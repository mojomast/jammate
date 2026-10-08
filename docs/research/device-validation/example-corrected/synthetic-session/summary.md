# Device-validation report

- session: `synthetic-selftest`
- gate requested: **all**
- overall pass: **False**
- hard errors: **0**
- matrix empty: **True**

## Hardware matrix

| condition | status | latency ms | target ms | observations |
|---|---|---|---|---|
| asio_48k_64 | missing | None | None | 0 |
| asio_48k_128 | missing | None | None | 0 |
| asio_48k_256 | missing | None | None | 0 |
| wasapi_low_latency | missing | None | None | 0 |
| device_disconnect_reconnect | missing | None | None | 0 |
| input_channel_change | missing | None | None | 0 |
| silent_input | missing | None | None | 0 |
| clipped_input | missing | None | None | 0 |

## Play trials

| condition | status | useful-lock | observations |
|---|---|---|---|
| clean_strumming | missing | None | 0 |
| distorted_rhythm | missing | None | 0 |
| palm_muted_metal | missing | None | 0 |
| blues_shuffle | missing | None | 0 |
| syncopated_funk | missing | None | 0 |
| sparse_single_note | missing | None | 0 |

## Gates

- **hardware_matrix**: FAIL
- **monitoring_latency**: FAIL
- **callback_deadline**: FAIL
- **play_trials**: FAIL
- **synthetic_selftest**: PASS
