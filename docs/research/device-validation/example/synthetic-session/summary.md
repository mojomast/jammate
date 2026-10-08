# Device-validation report

- session: `synthetic-selftest`
- gate requested: **all**
- overall pass: **False**
- hard errors: **0**
- matrix empty: **True**

## Hardware matrix

| condition | status | latency ms | target ms |
|---|---|---|---|
| asio_48k_64 | missing | None | None |
| asio_48k_128 | missing | None | None |
| asio_48k_256 | missing | None | None |
| wasapi_low_latency | missing | None | None |
| device_disconnect_reconnect | missing | None | None |
| input_channel_change | missing | None | None |
| silent_input | missing | None | None |
| clipped_input | missing | None | None |

## Play trials

| condition | status | useful-lock |
|---|---|---|
| clean_strumming | missing | None |
| distorted_rhythm | missing | None |
| palm_muted_metal | missing | None |
| blues_shuffle | missing | None |
| syncopated_funk | missing | None |
| sparse_single_note | missing | None |

## Gates

- **hardware_matrix**: FAIL
- **monitoring_latency**: FAIL
- **play_trials**: FAIL
- **synthetic_selftest**: PASS
