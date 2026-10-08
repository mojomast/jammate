// RT-005 deterministic differential test for the NAM activation allocation
// repair (PReLU + gating/blending).
//
// One source file is compiled twice, once against the pinned upstream NAM
// headers/library and once against the generated patched overlay. Each process
// runs the *same* deterministic activation and model workload and writes its raw
// float output plus a JSON summary. tools/nam-rt-repair/compare_activation_diff.py
// checks the pair within the predeclared tolerance.
//
// Sections:
//   * prelu        - ActivationPReLU::apply(MatrixXf&) and apply(float*, long)
//                    over coefficient vectors, channel counts and time lengths
//                    with edge-case and pseudo-random values.
//   * gating       - GatingActivation::apply over activation pairings (including
//                    PReLU as the primary activation) and shapes.
//   * blending     - BlendingActivation::apply over activation pairings
//                    (nontrivial PReLU primary + LeakyHardtanh blend) and shapes.
//   * model        - the real pinned example wavenet_a2_max.nam loaded through
//                    the same model API the processor uses, cold + warm blocks.
//
// Every section also records how many C++ `operator new`/`delete` calls happened
// inside the measured region. The pinned upstream process must show positive
// allocations for the PReLU-bearing sections (positive control); the patched
// process must show zero everywhere. That is the local proof that the repair
// removes the per-sample activation allocation.
//
// Fail-closed: malformed/out-of-range arguments, missing model and non-finite
// outputs are reported and exit non-zero after the artifacts are written.

#include <NAM/activations.h>
#include <NAM/gating_activations.h>
#include <NAM/get_dsp.h>

#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Global C++ allocation instrumentation.
//
// The RT-004 defect is C++ `operator new` traffic (a std::vector copy and, for
// other compilers, possible Eigen temporaries), so counting the global new
// family is the exact local counterpart of the probe's `cxxnew` column. These
// replacements must keep default visibility so they win over libstdc++'s weak
// definitions; the CMake target builds this TU with -fvisibility=default.
// ---------------------------------------------------------------------------
namespace
{
std::atomic<long> g_newCount{0};
std::atomic<long> g_delCount{0};

void resetCounts()
{
  g_newCount.store(0);
  g_delCount.store(0);
}
} // namespace

extern "C" void* malloc(std::size_t);
extern "C" void free(void*);

void* operator new (std::size_t n)
{
  g_newCount.fetch_add(1);
  return malloc (n == 0 ? 1 : n);
}
void* operator new[] (std::size_t n)
{
  g_newCount.fetch_add(1);
  return malloc (n == 0 ? 1 : n);
}
void* operator new (std::size_t n, const std::nothrow_t&) noexcept
{
  g_newCount.fetch_add(1);
  return malloc (n == 0 ? 1 : n);
}
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept
{
  g_newCount.fetch_add(1);
  return malloc (n == 0 ? 1 : n);
}
void* operator new (std::size_t n, std::align_val_t a)
{
  g_newCount.fetch_add(1);
  void* p = nullptr;
  if (posix_memalign (&p, static_cast<std::size_t> (a), n == 0 ? 1 : n) != 0)
    throw std::bad_alloc();
  return p;
}
void* operator new[] (std::size_t n, std::align_val_t a)
{
  g_newCount.fetch_add(1);
  void* p = nullptr;
  if (posix_memalign (&p, static_cast<std::size_t> (a), n == 0 ? 1 : n) != 0)
    throw std::bad_alloc();
  return p;
}
void* operator new (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept
{
  g_newCount.fetch_add(1);
  void* p = nullptr;
  if (posix_memalign (&p, static_cast<std::size_t> (a), n == 0 ? 1 : n) != 0)
    return nullptr;
  return p;
}
void* operator new[] (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept
{
  g_newCount.fetch_add(1);
  void* p = nullptr;
  if (posix_memalign (&p, static_cast<std::size_t> (a), n == 0 ? 1 : n) != 0)
    return nullptr;
  return p;
}
void operator delete (void* p) noexcept
{
  if (p != nullptr) g_delCount.fetch_add(1);
  free (p);
}
void operator delete[] (void* p) noexcept
{
  if (p != nullptr) g_delCount.fetch_add(1);
  free (p);
}
void operator delete (void* p, std::size_t) noexcept
{
  if (p != nullptr) g_delCount.fetch_add(1);
  free (p);
}
void operator delete[] (void* p, std::size_t) noexcept
{
  if (p != nullptr) g_delCount.fetch_add(1);
  free (p);
}
void operator delete (void* p, const std::nothrow_t&) noexcept
{
  if (p != nullptr) g_delCount.fetch_add(1);
  free (p);
}
void operator delete[] (void* p, const std::nothrow_t&) noexcept
{
  if (p != nullptr) g_delCount.fetch_add(1);
  free (p);
}
void operator delete (void* p, std::align_val_t) noexcept
{
  if (p != nullptr) g_delCount.fetch_add(1);
  free (p);
}
void operator delete[] (void* p, std::align_val_t) noexcept
{
  if (p != nullptr) g_delCount.fetch_add(1);
  free (p);
}
void operator delete (void* p, std::size_t, std::align_val_t) noexcept
{
  if (p != nullptr) g_delCount.fetch_add(1);
  free (p);
}
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept
{
  if (p != nullptr) g_delCount.fetch_add(1);
  free (p);
}

namespace
{

// --- deterministic weights / signal -----------------------------------------
std::uint64_t mix (std::uint64_t x)
{
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
  return x ^ (x >> 31);
}

float unit (std::uint64_t& s)
{
  s = mix (s);
  return (float) ((s >> 11) & 0xFFFFFFu) / 16777215.0f; // [0,1)
}

// Edge values first, then pseudo-random values spanning negative/positive and
// dynamic range extremes (never NaN/Inf so all_finite stays true). The largest
// magnitude is 1e30 so a PReLU slope of up to 1e8 still yields a finite product.
float edgeValue (int index, int salt, std::uint64_t& s)
{
  static const float kEdge[] = {
    0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f,
    1.0e-30f, -1.0e-30f, 1.0e8f, -1.0e8f, 1.0e20f, -1.0e20f,
    std::numeric_limits<float>::denorm_min(),
    -std::numeric_limits<float>::denorm_min(),
    1.0e30f, -1.0e30f,
    0.01f, -0.01f, 2.0f, -2.0f,
  };
  const int nEdge = (int) (sizeof (kEdge) / sizeof (kEdge[0]));
  if (index < nEdge)
    return kEdge[index];
  const float u = unit (s);
  const float v = (u * 2.0f - 1.0f) * 4.0f; // [-4,4)
  return v + (float) ((salt % 7) - 3) * 0.125f;
}

// Bounded generator for the gating/blending combinations, where two activations
// are multiplied. Magnitudes stay within +/-16 so no product can overflow even
// for the largest PReLU slope; tiny/subnormal and signed-zero edge cases remain.
float edgeValueBounded (int index, int salt, std::uint64_t& s)
{
  static const float kEdge[] = {
    0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 2.0f, -2.0f, 4.0f, -4.0f,
    8.0f, -8.0f, 16.0f, -16.0f,
    1.0e-8f, -1.0e-8f, 1.0e-4f, -1.0e-4f,
    0.01f, -0.01f,
  };
  const int nEdge = (int) (sizeof (kEdge) / sizeof (kEdge[0]));
  if (index < nEdge)
    return kEdge[index];
  const float u = unit (s);
  const float v = (u * 2.0f - 1.0f) * 3.0f; // [-3,3)
  return v + (float) ((salt % 5) - 2) * 0.03125f;
}

std::string sha256File (const char* path, bool& ok)
{
  // Keep the test dependency-free: hash with a small FNV over the bytes for a
  // stable model identity in the summary, and a separate full read for
  // existence. The comparator additionally cross-checks the two processes used
  // the same model bytes.
  std::FILE* f = std::fopen (path, "rb");
  if (f == nullptr)
  {
    ok = false;
    return std::string();
  }
  std::uint64_t h = 1469598103934665603ull;
  unsigned char buf[8192];
  std::size_t n;
  while ((n = std::fread (buf, 1, sizeof (buf), f)) > 0)
    for (std::size_t i = 0; i < n; ++i)
    {
      h ^= buf[i];
      h *= 1099511628211ull;
    }
  const bool closed = std::fclose (f) == 0;
  ok = closed;
  char out[32];
  std::snprintf (out, sizeof (out), "%016llx", (unsigned long long) h);
  return std::string (out);
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

struct Section
{
  std::string name;
  std::string kind;
  std::size_t offset = 0;
  std::size_t count = 0;
  long alloc = 0;
  long freeCount = 0;
  bool expectOrigPositive = false;
};

// --- predeclared workload tables --------------------------------------------
const std::vector<std::vector<float>>& preluSlopes()
{
  static const std::vector<std::vector<float>> k = {
    { 0.01f },
    { 0.0f },
    { -0.5f },
    { 2.0f },
    { 1.0e-8f },
    { 1.0e8f },
    { 0.04f, 0.05f },
    { 0.03f, 0.01f },
    { 1.0f, 2.0f, 3.0f },
    { -0.1f, 0.2f, -0.3f, 0.4f },
    { 0.01f, 0.02f, 0.03f, 0.04f, 0.05f },
    { 0.11f, -0.22f, 0.33f, -0.44f, 0.55f, -0.66f, 0.77f, -0.88f },
  };
  return k;
}

const std::vector<int>& testLengths()
{
  static const std::vector<int> k = { 1, 2, 3, 7, 16, 64 };
  return k;
}

nam::activations::Activation::Ptr makeActivation (const std::string& name, int channels)
{
  using namespace nam::activations;
  if (name == "identity")
    return std::make_shared<ActivationIdentity>();
  if (name == "tanh")
    return std::make_shared<ActivationTanh>();
  if (name == "relu")
    return std::make_shared<ActivationReLU>();
  if (name == "leakyrelu")
    return std::make_shared<ActivationLeakyReLU> (0.01f);
  if (name == "leakyhardtanh")
    return std::make_shared<ActivationLeakyHardTanh> (0.0f, 0.9f, 0.0f, 0.02f);
  if (name == "softsign")
    return std::make_shared<ActivationSoftsign>();
  if (name == "silu")
    return std::make_shared<ActivationSwish>();
  if (name == "sigmoid")
    return std::make_shared<ActivationSigmoid>();
  if (name == "hardswish")
    return std::make_shared<ActivationHardSwish>();
  if (name == "prelu1")
  {
    // A uniform per-channel PReLU. A single-slope PReLU is only valid for a
    // one-row matrix (the upstream debug assert is compiled out under NDEBUG),
    // so the multi-channel sections use one slope per channel.
    std::vector<float> slopes ((std::size_t) channels, 0.01f);
    return std::make_shared<ActivationPReLU> (slopes);
  }
  if (name == "prelu_per_channel")
  {
    std::vector<float> slopes;
    slopes.reserve ((std::size_t) channels);
    for (int c = 0; c < channels; ++c)
      slopes.push_back (0.01f + 0.017f * (float) ((c * 5) % 11) - 0.03f * (float) (c % 3));
    return std::make_shared<ActivationPReLU> (slopes);
  }
  throw std::runtime_error ("unknown activation spec");
}

} // namespace

int main (int argc, char** argv)
{
  const char* binPath = nullptr;
  const char* jsonPath = nullptr;
  const char* modelPath = nullptr;
  std::uint32_t seed = 0x5eed1234u;
  long warmBlocks = 64;
  long block = 128;

  for (int i = 1; i < argc; ++i)
  {
    if (std::strcmp (argv[i], "--out") == 0 && i + 1 < argc) binPath = argv[++i];
    else if (std::strcmp (argv[i], "--json") == 0 && i + 1 < argc) jsonPath = argv[++i];
    else if (std::strcmp (argv[i], "--model") == 0 && i + 1 < argc) modelPath = argv[++i];
    else if (std::strcmp (argv[i], "--seed") == 0 && i + 1 < argc)
    {
      if (! parseU32 (argv[++i], seed))
      { std::fprintf (stderr, "error: invalid --seed\n"); return 64; }
    }
    else if (std::strcmp (argv[i], "--warm-blocks") == 0 && i + 1 < argc)
    {
      if (! parseInt (argv[++i], 1, 1000000, warmBlocks))
      { std::fprintf (stderr, "error: invalid --warm-blocks\n"); return 64; }
    }
    else if (std::strcmp (argv[i], "--block") == 0 && i + 1 < argc)
    {
      if (! parseInt (argv[++i], 1, 65536, block))
      { std::fprintf (stderr, "error: invalid --block\n"); return 64; }
    }
    else { std::fprintf (stderr, "unknown option %s\n", argv[i]); return 64; }
  }
  if (binPath == nullptr || jsonPath == nullptr || modelPath == nullptr)
  {
    std::fprintf (stderr, "usage: %s --out f.bin --json f.json --model model.nam "
                         "[--seed N] [--warm-blocks N] [--block N]\n", argv[0]);
    return 64;
  }
  bool modelOk = false;
  const std::string modelHash = sha256File (modelPath, modelOk);
  if (! modelOk)
  {
    std::fprintf (stderr, "error: cannot read model %s\n", modelPath);
    return 64;
  }

  std::vector<float> output;
  std::vector<Section> sections;

  auto measure = [&] (const char* name, const char* kind, std::size_t nfloats,
                      bool expectOrigPositive, auto&& fn)
  {
    const std::size_t off = output.size();
    output.resize (off + nfloats); // capacity reserved before the counter reset
    resetCounts();
    fn (output.data() + off);
    Section s;
    s.name = name;
    s.kind = kind;
    s.offset = off;
    s.count = nfloats;
    s.alloc = g_newCount.load();
    s.freeCount = g_delCount.load();
    s.expectOrigPositive = expectOrigPositive;
    sections.push_back (s);
  };

  // ==========================================================================
  // Section 1: PReLU direct (MatrixXf and raw-pointer entry points).
  // ==========================================================================
  {
    struct Case
    {
      nam::activations::ActivationPReLU act;
      Eigen::MatrixXf mat;
      std::vector<float> raw;
      int rows;
      int cols;
    };
    std::vector<Case> cases;
    std::uint64_t ws = ((std::uint64_t) seed << 32) ^ 0x9e3779b97f4a7c15ull;
    const auto& slopesList = preluSlopes();
    const auto& lengths = testLengths();
    for (std::size_t si = 0; si < slopesList.size(); ++si)
    {
      const int rows = (int) slopesList[si].size();
      for (std::size_t li = 0; li < lengths.size(); ++li)
      {
        const int cols = lengths[li];
        Case c;
        c.act = nam::activations::ActivationPReLU (slopesList[si]);
        c.rows = rows;
        c.cols = cols;
        c.mat.resize (rows, cols);
        c.raw.resize ((std::size_t) rows * (std::size_t) cols);
        int index = 0;
        for (int r = 0; r < rows; ++r)
          for (int t = 0; t < cols; ++t)
          {
            const float v = edgeValue (index++, (int) si + r, ws);
            c.mat (r, t) = v;
            c.raw[(std::size_t) r * (std::size_t) cols + (std::size_t) t] = v;
          }
        cases.push_back (std::move (c));
      }
    }

    std::size_t total = 0;
    for (const auto& c : cases)
      total += (std::size_t) c.rows * (std::size_t) c.cols * 2; // matrix + raw copy

    measure ("prelu", "prelu", total, true, [&] (float* dst) {
      std::size_t k = 0;
      for (auto& c : cases)
      {
        c.act.apply (c.mat);
        for (int r = 0; r < c.rows; ++r)
          for (int t = 0; t < c.cols; ++t)
            dst[k++] = c.mat (r, t);
        c.act.apply (c.raw.data(), (long) c.raw.size());
        for (float v : c.raw)
          dst[k++] = v;
      }
    });
  }

  // ==========================================================================
  // Section 2 & 3: Gating and blending direct.
  // ==========================================================================
  {
    struct Combo
    {
      std::unique_ptr<nam::gating_activations::GatingActivation> gating;
      std::unique_ptr<nam::gating_activations::BlendingActivation> blending;
      Eigen::MatrixXf input;
      Eigen::MatrixXf outG;
      Eigen::MatrixXf outB;
      int channels;
      int samples;
    };
    struct Pair
    {
      const char* primary;
      const char* secondary;
    };
    static const Pair kPairs[] = {
      { "prelu_per_channel", "leakyhardtanh" },
      { "prelu1", "relu" },
      { "prelu_per_channel", "sigmoid" },
      { "tanh", "leakyhardtanh" },
      { "relu", "relu" },
      { "softsign", "sigmoid" },
      { "silu", "prelu_per_channel" },
      { "identity", "tanh" },
    };
    static const int kChannels[] = { 1, 2, 3, 4, 8 };
    static const int kSamples[] = { 1, 2, 3, 5, 17, 64 };

    std::vector<Combo> combos;
    std::uint64_t ws = ((std::uint64_t) seed << 17) ^ 0xD1B54A32D192ED03ull;
    for (const auto& pair : kPairs)
      for (int channels : kChannels)
        for (int samples : kSamples)
        {
          Combo c;
          c.channels = channels;
          c.samples = samples;
          auto ia = makeActivation (pair.primary, channels);
          auto sa = makeActivation (pair.secondary, channels);
          c.gating = std::make_unique<nam::gating_activations::GatingActivation> (ia, sa, channels);
          c.blending = std::make_unique<nam::gating_activations::BlendingActivation> (ia, sa, channels);
          c.input.resize (2 * channels, samples);
          c.outG.resize (channels, samples);
          c.outB.resize (channels, samples);
          int index = 0;
          for (int r = 0; r < 2 * channels; ++r)
            for (int t = 0; t < samples; ++t)
              c.input (r, t) = edgeValueBounded (index++, channels + samples, ws);
          combos.push_back (std::move (c));
        }

    std::size_t gatingTotal = 0, blendingTotal = 0;
    for (const auto& c : combos)
    {
      gatingTotal += (std::size_t) c.channels * (std::size_t) c.samples;
      blendingTotal += (std::size_t) c.channels * (std::size_t) c.samples;
    }

    measure ("gating", "gating", gatingTotal, true, [&] (float* dst) {
      std::size_t k = 0;
      for (auto& c : combos)
      {
        auto input = c.input.leftCols (c.samples);
        auto out = c.outG.topRows (c.channels).leftCols (c.samples);
        c.gating->apply (input, out);
        for (int r = 0; r < c.channels; ++r)
          for (int t = 0; t < c.samples; ++t)
            dst[k++] = c.outG (r, t);
      }
    });

    measure ("blending", "blending", blendingTotal, true, [&] (float* dst) {
      std::size_t k = 0;
      for (auto& c : combos)
      {
        auto input = c.input.leftCols (c.samples);
        auto out = c.outB.topRows (c.channels).leftCols (c.samples);
        c.blending->apply (input, out);
        for (int r = 0; r < c.channels; ++r)
          for (int t = 0; t < c.samples; ++t)
            dst[k++] = c.outB (r, t);
      }
    });
  }

  // ==========================================================================
  // Section 4: the real pinned example model (wavenet_a2_max), cold + warm.
  // ==========================================================================
  {
    std::unique_ptr<nam::DSP> model;
    try
    {
      model = nam::get_dsp (std::filesystem::path (modelPath));
    }
    catch (const std::exception& e)
    {
      std::fprintf (stderr, "error: get_dsp(%s) failed: %s\n", modelPath, e.what());
      return 5;
    }
    if (! model)
    {
      std::fprintf (stderr, "error: get_dsp(%s) returned null\n", modelPath);
      return 5;
    }
    const int inCh = model->NumInputChannels();
    const int outCh = model->NumOutputChannels();
    if (inCh <= 0 || outCh <= 0)
    {
      std::fprintf (stderr, "error: model has invalid channel counts %d/%d\n", inCh, outCh);
      return 5;
    }
    model->Reset (48000.0, (int) block);

    std::vector<std::vector<float>> inBuf ((std::size_t) inCh);
    std::vector<std::vector<float>> outBuf ((std::size_t) outCh);
    std::vector<NAM_SAMPLE*> inPtr ((std::size_t) inCh);
    std::vector<NAM_SAMPLE*> outPtr ((std::size_t) outCh);
    for (int c = 0; c < inCh; ++c)
    {
      inBuf[(std::size_t) c].assign ((std::size_t) block, 0.0f);
      inPtr[(std::size_t) c] = inBuf[(std::size_t) c].data();
    }
    for (int c = 0; c < outCh; ++c)
    {
      outBuf[(std::size_t) c].assign ((std::size_t) block, 0.0f);
      outPtr[(std::size_t) c] = outBuf[(std::size_t) c].data();
    }
    const std::size_t frames = (std::size_t) warmBlocks + 1;
    const std::size_t modelTotal = (std::size_t) outCh * (std::size_t) block * frames;

    measure ("model", "model", modelTotal, true, [&] (float* dst) {
      std::size_t k = 0;
      std::uint64_t s = ((std::uint64_t) seed << 32) ^ 0xA5A5A5A5A5A5A5A5ull;
      for (long b = 0; b <= warmBlocks; ++b)
      {
        const int type = (int) (b % 5);
        for (int c = 0; c < inCh; ++c)
          for (long i = 0; i < block; ++i)
          {
            const float t = block > 1 ? (float) i / (float) (block - 1) : 0.0f;
            float v = 0.0f;
            switch (type)
            {
              case 0: v = (t - 0.5f) * (c == 0 ? 1.0f : 0.7f); break;
              case 1: v = 0.4f * std::sin (6.2831853f * (0.013f + 0.004f * (float) c) * (float) i); break;
              case 2: v = (unit (s) - 0.5f) * 0.6f; break;
              case 3: v = (i % 32 == 0) ? ((c & 1) ? -0.8f : 0.8f) : 0.0f; break;
              default: v = 0.0f; break;
            }
            inBuf[(std::size_t) c][(std::size_t) i] = v;
          }
        model->process (inPtr.data(), outPtr.data(), (int) block);
        for (int c = 0; c < outCh; ++c)
          for (long i = 0; i < block; ++i)
            dst[k++] = outBuf[(std::size_t) c][(std::size_t) i];
      }
    });
  }

  // --- finiteness, then serialize ---------------------------------------------
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
  std::fprintf (jf, "{\n  \"seed\": %u,\n  \"model\": \"%s\",\n  \"model_fnv\": \"%s\",\n"
                    "  \"warm_blocks\": %ld,\n  \"block\": %ld,\n  \"all_finite\": %s,\n"
                    "  \"total_floats\": %zu,\n  \"sections\": [\n",
                seed, modelPath, modelHash.c_str(), warmBlocks, block,
                allFinite ? "true" : "false", output.size());
  for (std::size_t si = 0; si < sections.size(); ++si)
  {
    const Section& s = sections[si];
    double sum = 0.0, sumsq = 0.0;
    float mn = 0.0f, mx = 0.0f;
    std::uint32_t h = 2166136261u;
    for (std::size_t k = 0; k < s.count; ++k)
    {
      const float v = output[s.offset + k];
      sum += v;
      sumsq += (double) v * v;
      if (k == 0 || v < mn) mn = v;
      if (k == 0 || v > mx) mx = v;
      h = fnv1a (h, v);
    }
    std::fprintf (jf,
      "    {\"name\": \"%s\", \"kind\": \"%s\", \"offset\": %zu, \"count\": %zu, "
      "\"sum\": %.9g, \"sumsq\": %.9g, \"min\": %.9g, \"max\": %.9g, \"fnv1a\": %u, "
      "\"alloc\": %ld, \"free\": %ld, \"expect_orig_positive\": %s}%s\n",
      s.name.c_str(), s.kind.c_str(), s.offset, s.count, sum, sumsq,
      (double) mn, (double) mx, h, s.alloc, s.freeCount,
      s.expectOrigPositive ? "true" : "false",
      (si + 1 < sections.size()) ? "," : "");
  }
  std::fprintf (jf, "  ]\n}\n");
  const bool jw = std::ferror (jf) == 0;
  const bool jc = std::fclose (jf) == 0;
  if (! jw || ! jc) { std::fprintf (stderr, "json write/close failure %s\n", jsonPath); return 5; }

  std::printf ("nam_activation_diff: %zu sections, %zu floats (block=%ld warm=%ld finite=%d) -> %s\n",
               sections.size(), output.size(), block, warmBlocks, (int) allFinite, binPath);
  for (const Section& s : sections)
    std::printf ("  %-10s %-8s n=%-8zu alloc=%-6ld free=%-6ld expect_orig_positive=%d\n",
                 s.name.c_str(), s.kind.c_str(), s.count, s.alloc, s.freeCount,
                 (int) s.expectOrigPositive);
  if (! allFinite)
  {
    std::fprintf (stderr, "error: non-finite output detected\n");
    return 6;
  }
  return 0;
}
