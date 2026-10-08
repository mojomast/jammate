// RT-003 deterministic differential test for the NAM LSTM real-time allocation
// repair.
//
// One source file is compiled twice, once against the pinned upstream NAM
// headers/library and once against the generated patched overlay. Each process
// runs the *same* deterministic configurations and signal, and writes its raw
// float output plus a JSON summary. tools/nam-rt-repair/compare_diff.py checks
// the pair within a predeclared tolerance.
//
// The test only uses the model-level DSP API (constructor + process), whose
// signature/ABI is unchanged by the repair, so the identical source compiles
// against both header variants.
//
// Signal coverage per block cycles ramp / sine / uniform noise / impulses /
// silence; every block is freshly seeded from (seed, block, config), so the two
// processes are reproducible and independent.
//
// Fail-closed: malformed/out-of-range arguments and non-finite outputs are
// reported and exit non-zero after the artifacts are written.

#include <NAM/dsp.h>
#include <NAM/lstm.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

// Hard bounds so a bad CLI cannot request an unbounded allocation.
constexpr long kMaxBlock = 65536;
constexpr long kMaxWarm = 1000000;
constexpr long long kMaxFrames = 2000000;   // block * (warm+1) ceiling

struct Config
{
  const char* name;
  int in_channels;
  int out_channels;
  int num_layers;
  int input_size;
  int hidden_size;
};

const Config kConfigs[] = {
  { "mono_1x3",      1, 1, 1,  1,  3 },
  { "mono_2x5",      1, 1, 2,  1,  5 },
  { "mono_4x16",     1, 1, 4,  1, 16 },
  { "stereo_3x7",    2, 2, 3,  2,  7 },
  { "multi_io_2x11", 3, 2, 2,  3, 11 },
  { "zero_layer",    2, 3, 0,  2,  4 },
};
constexpr int kNumConfigs = (int) (sizeof(kConfigs) / sizeof(kConfigs[0]));

// splitmix64 -> deterministic weights
std::uint64_t mix (std::uint64_t x)
{
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
  return x ^ (x >> 31);
}

float nextWeight (std::uint64_t& s)
{
  s = mix (s);
  const float u = (float) ((s >> 11) & 0xFFFFFFu) / 16777215.0f;   // [0,1)
  return (u - 0.5f);                                              // [-0.5,0.5)
}

std::size_t weightCount (const Config& c)
{
  std::size_t n = 0;
  for (int l = 0; l < c.num_layers; ++l)
  {
    const int in = (l == 0) ? c.input_size : c.hidden_size;
    n += (std::size_t) 4 * c.hidden_size * (in + c.hidden_size);   // _w
    n += (std::size_t) 4 * c.hidden_size;                          // _b
    n += (std::size_t) c.hidden_size;                              // _xh hidden init
    n += (std::size_t) c.hidden_size;                              // _c
  }
  n += (std::size_t) c.out_channels * c.hidden_size;               // head weight
  n += (std::size_t) c.out_channels;                               // head bias
  return n;
}

// Fill one block. type = block % 5; deterministic per (seed, block, config).
void fillBlock (std::vector<float>& chans, int inCh, int block, int n,
                std::uint32_t seed, int configIndex)
{
  std::uint64_t s = ((std::uint64_t) seed << 32) ^ ((std::uint64_t) block * 2654435761ull)
                    ^ ((std::uint64_t) configIndex * 0x9e3779b97f4a7c15ull);
  for (int c = 0; c < inCh; ++c)
    for (int i = 0; i < n; ++i)
    {
      const int type = block % 5;
      float v = 0.0f;
      const float t = n > 1 ? (float) i / (float) (n - 1) : 0.0f;
      switch (type)
      {
        case 0: v = (t - 0.5f) * (c == 0 ? 1.0f : 0.7f); break;                  // ramp
        case 1: v = 0.4f * std::sin (6.2831853f * (0.013f + 0.004f * (float) c) * (float) i); break;  // sine
        case 2: { s = mix (s); v = (((float) ((s >> 11) & 0xFFFFu) / 65535.0f) - 0.5f) * 0.6f; break; } // noise
        case 3: v = (i % 32 == 0) ? ((c & 1) ? -0.8f : 0.8f) : 0.0f; break;       // impulses
        default: v = 0.0f; break;                                                // silence
      }
      chans[(std::size_t) c * n + i] = v;
    }
}

std::uint32_t fnv1a (std::uint32_t h, float f)
{
  std::uint32_t b;
  std::memcpy (&b, &f, sizeof (b));
  for (int k = 0; k < 4; ++k)
  {
    h ^= (b >> (8 * k)) & 0xFFu;
    h *= 16777619u;
  }
  return h;
}

bool parseInt (const char* s, long lo, long hi, long& out)
{
  errno = 0;
  char* end = nullptr;
  const long v = std::strtol (s, &end, 10);
  if (errno != 0 || end == s || end == nullptr || *end != '\0' || v < lo || v > hi)
    return false;
  out = v;
  return true;
}

bool parseU32 (const char* s, std::uint32_t& out)
{
  errno = 0;
  char* end = nullptr;
  const unsigned long v = std::strtoul (s, &end, 0);
  if (errno != 0 || end == s || end == nullptr || *end != '\0' || v > 0xFFFFFFFFul)
    return false;
  out = (std::uint32_t) v;
  return true;
}

} // namespace

int main (int argc, char** argv)
{
  const char* binPath = nullptr;
  const char* jsonPath = nullptr;
  std::uint32_t seed = 0x5eed1234u;
  long warmBlocks = 64;
  long block = 128;

  for (int i = 1; i < argc; ++i)
  {
    if (std::strcmp (argv[i], "--out") == 0 && i + 1 < argc) binPath = argv[++i];
    else if (std::strcmp (argv[i], "--json") == 0 && i + 1 < argc) jsonPath = argv[++i];
    else if (std::strcmp (argv[i], "--seed") == 0 && i + 1 < argc)
    {
      if (! parseU32 (argv[++i], seed))
      { std::fprintf (stderr, "error: invalid --seed\n"); return 64; }
    }
    else if (std::strcmp (argv[i], "--warm-blocks") == 0 && i + 1 < argc)
    {
      if (! parseInt (argv[++i], 1, kMaxWarm, warmBlocks))
      { std::fprintf (stderr, "error: --warm-blocks must be 1..%ld\n", kMaxWarm); return 64; }
    }
    else if (std::strcmp (argv[i], "--block") == 0 && i + 1 < argc)
    {
      if (! parseInt (argv[++i], 1, kMaxBlock, block))
      { std::fprintf (stderr, "error: --block must be 1..%ld\n", kMaxBlock); return 64; }
    }
    else { std::fprintf (stderr, "unknown option %s\n", argv[i]); return 64; }
  }
  if (binPath == nullptr || jsonPath == nullptr
      || (long long) block * (warmBlocks + 1) > kMaxFrames)
  {
    std::fprintf (stderr, "usage: %s --out f.bin --json f.json [--seed N] "
                         "[--warm-blocks N] [--block N]; block*(warm+1) <= %lld\n",
                  argv[0], kMaxFrames);
    return 64;
  }

  std::vector<float> output;
  std::vector<std::string> names;
  std::vector<std::size_t> offsets;
  std::vector<std::size_t> counts;

  for (int ci = 0; ci < kNumConfigs; ++ci)
  {
    const Config& c = kConfigs[ci];
    std::uint64_t ws = 0x123456789abcdef0ull
                       ^ ((std::uint64_t) (seed + 1) << 17)
                       ^ ((std::uint64_t) ci * 0xD1B54A32D192ED03ull);
    std::vector<float> weights (weightCount (c));
    for (auto& w : weights) w = nextWeight (ws);

    nam::lstm::LSTM model (c.in_channels, c.out_channels, c.num_layers,
                           c.input_size, c.hidden_size, weights, 48000.0);

    const std::size_t off = output.size();
    std::vector<float> inbuf ((std::size_t) c.in_channels * (std::size_t) block);
    std::vector<float> outbuf ((std::size_t) c.out_channels * (std::size_t) block);
    std::vector<NAM_SAMPLE*> inPtr ((std::size_t) c.in_channels);
    std::vector<NAM_SAMPLE*> outPtr ((std::size_t) c.out_channels);
    for (int ch = 0; ch < c.in_channels; ++ch)
      inPtr[(std::size_t) ch] = inbuf.data() + (std::size_t) ch * (std::size_t) block;
    for (int ch = 0; ch < c.out_channels; ++ch)
      outPtr[(std::size_t) ch] = outbuf.data() + (std::size_t) ch * (std::size_t) block;

    // cold block (0) then warm blocks 1..warmBlocks; all recorded.
    for (long b = 0; b <= warmBlocks; ++b)
    {
      fillBlock (inbuf, c.in_channels, (int) b, (int) block, seed, ci);
      model.process (inPtr.data(), outPtr.data(), (int) block);
      for (int ch = 0; ch < c.out_channels; ++ch)
        for (long i = 0; i < block; ++i)
          output.push_back (outbuf[(std::size_t) ch * (std::size_t) block + (std::size_t) i]);
    }
    names.push_back (c.name);
    offsets.push_back (off);
    counts.push_back (output.size() - off);
  }

  bool allFinite = true;
  for (float v : output)
    if (! std::isfinite (v)) { allFinite = false; break; }

  std::FILE* bf = std::fopen (binPath, "wb");
  if (bf == nullptr) { std::fprintf (stderr, "cannot write %s\n", binPath); return 5; }
  const bool bw = std::fwrite (output.data(), sizeof (float), output.size(), bf) == output.size();
  const bool bc = std::fclose (bf) == 0;
  if (! bw || ! bc) { std::fprintf (stderr, "write/close failure %s\n", binPath); return 5; }

  std::FILE* jf = std::fopen (jsonPath, "w");
  if (jf == nullptr) { std::fprintf (stderr, "cannot write %s\n", jsonPath); return 5; }
  std::fprintf (jf, "{\n  \"seed\": %u,\n  \"warm_blocks\": %ld,\n  \"block\": %ld,\n"
                    "  \"all_finite\": %s,\n  \"configs\": [\n",
                seed, warmBlocks, block, allFinite ? "true" : "false");
  for (int ci = 0; ci < kNumConfigs; ++ci)
  {
    const Config& c = kConfigs[ci];
    const auto o = offsets[(std::size_t) ci];
    const auto n = counts[(std::size_t) ci];
    double sum = 0.0, sumsq = 0.0;
    float mn = 0.0f, mx = 0.0f;
    std::uint32_t h = 2166136261u;
    for (std::size_t k = 0; k < n; ++k)
    {
      const float v = output[o + k];
      sum += v; sumsq += (double) v * v;
      if (k == 0 || v < mn) mn = v;
      if (k == 0 || v > mx) mx = v;
      h = fnv1a (h, v);
    }
    std::fprintf (jf,
      "    {\"name\": \"%s\", \"in_channels\": %d, \"out_channels\": %d, "
      "\"num_layers\": %d, \"input_size\": %d, \"hidden_size\": %d, "
      "\"offset\": %zu, \"count\": %zu, \"sum\": %.9g, "
      "\"sumsq\": %.9g, \"min\": %.9g, \"max\": %.9g, \"fnv1a\": %u}%s\n",
      c.name, c.in_channels, c.out_channels, c.num_layers, c.input_size, c.hidden_size,
      o, n, sum, sumsq, (double) mn, (double) mx, h,
      (ci + 1 < kNumConfigs) ? "," : "");
  }
  std::fprintf (jf, "  ]\n}\n");
  const bool jw = std::ferror (jf) == 0;
  const bool jc = std::fclose (jf) == 0;
  if (! jw || ! jc) { std::fprintf (stderr, "json write/close failure %s\n", jsonPath); return 5; }

  std::printf ("nam_lstm_diff: %d configs, %zu floats (block=%ld warm=%ld finite=%d) -> %s\n",
               kNumConfigs, output.size(), block, warmBlocks, (int) allFinite, binPath);
  if (! allFinite)
  {
    std::fprintf (stderr, "error: non-finite output detected\n");
    return 6;
  }
  return 0;
}
