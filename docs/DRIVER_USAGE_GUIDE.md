# Hướng Dẫn Sử Dụng Driver ADT7410 & AXI IIC HAL (`DRIVER_USAGE_GUIDE`)

Tài liệu này hướng dẫn tầng ứng dụng (`Application / Health Monitor`) các bước
khởi tạo, cấu hình, đọc nhiệt độ, xử lý cảnh báo và quản lý lỗi khi tích hợp
module **AXI IIC HAL** (`include/axi_iic_hal.h`) và **ADT7410 Driver**
(`include/adt7410_driver.h`).

---

## 1. Nguyên Tắc Tích Hợp Bắt Buộc (Safety & Concurrency)

Trước khi gọi API, tầng ứng dụng cần đảm bảo 4 nguyên tắc thiết kế từ
`docs/SDD_ADT7410_AXI_IIC.md` (§2.2, §4.3):

1. **Cấp phát tĩnh (No Dynamic Memory)**: Driver không dùng `malloc`/`free`.
   Tầng ứng dụng tự cấp phát `axi_iic_dev_t` và `adt7410_dev_t` (biến `static`
   hoặc thuộc context của task). Biến `axi_iic_dev_t` **phải tồn tại suốt vòng
   đời** của mọi `adt7410_dev_t` trỏ tới nó.
2. **Không gọi trong ngắt (Not ISR-safe)**: Mọi hàm đều thực hiện I/O kiểu
   polling có giới hạn (`timeout_polls`). Không gọi trực tiếp từ ISR.
3. **Đồng bộ hóa khi dùng chung bus I2C**: Có thể gắn tối đa 4 cảm biến ADT7410
   (`0x48`..`0x4B`) trên cùng một bộ điều khiển `axi_iic_dev_t`. Nếu nhiều RTOS
   task cùng truy cập các cảm biến trên **cùng một `axi_iic_dev_t`**, tầng ứng
   dụng phải khóa bằng mutex trước mỗi lời gọi API.
4. **Luôn kiểm tra mã trả về**: Mọi hàm có thể thất bại đều trả về
   `axi_iic_status_t` hoặc `adt7410_status_t`. Dữ liệu đầu ra chỉ hợp lệ khi
   hàm trả về `AXI_IIC_OK` (`0U`) hoặc `ADT7410_OK` (`0U`).

---

## 2. Quy Trình Sử Dụng Từng Bước

### Bước 1: Khởi tạo bộ điều khiển bus AXI IIC (`axi_iic_init`)

Cung cấp địa chỉ vật lý MMIO của khối IP Xilinx AXI IIC (`base_addr != 0U`) và
số vòng lặp chờ tối đa cho mỗi trạng thái FIFO/bus (`timeout_polls` trong khoảng
`1..AXI_IIC_MAX_POLL_LIMIT`, mặc định dùng `AXI_IIC_DEFAULT_POLLS`).

> **Lưu ý chọn `timeout_polls`**: Chọn giá trị sao cho `timeout_polls × t_poll`
> lớn hơn thời gian truyền 1 byte chậm nhất trên bus I2C (~90 µs ở 100 kHz cộng
> thời gian slave giữ xung clock nếu có).

```c
axi_iic_dev_t iic_bus;
axi_iic_status_t iic_status;

iic_status = axi_iic_init(&iic_bus,
                          (uintptr_t)0x40800000U,
                          AXI_IIC_DEFAULT_POLLS);
```

### Bước 2: Khởi tạo và định danh cảm biến ADT7410 (`adt7410_init`)

Chuẩn bị cấu hình ban đầu `adt7410_config_t` và địa chỉ 7-bit của cảm biến theo
chân phần cứng `A1`, `A0`:
- `ADT7410_I2C_ADDR_A1L_A0L` (`0x48U`)
- `ADT7410_I2C_ADDR_A1L_A0H` (`0x49U`)
- `ADT7410_I2C_ADDR_A1H_A0L` (`0x4AU`)
- `ADT7410_I2C_ADDR_A1H_A0H` (`0x4BU`)

Hàm `adt7410_init()` sẽ tự động:
1. Đọc thanh ghi ID (`0x0B`) và kiểm tra Manufacturer ID phải bằng `0x19`
   (nếu sai trả về `ADT7410_ERR_DEV_ID`).
2. Ghi cấu hình vào thanh ghi Configuration (`0x03`).
3. Lưu độ phân giải (`active_res`) và chuyển trạng thái sang
   `ADT7410_STATE_READY`.

```c
adt7410_dev_t sensor;
adt7410_status_t status;
adt7410_config_t const init_cfg =
{
    .resolution   = ADT7410_RES_16BIT,        /* 0.0078125 degC/LSB */
    .op_mode      = ADT7410_MODE_CONTINUOUS,  /* Chuyen doi lien tuc */
    .event_mode   = ADT7410_EVENT_COMPARATOR, /* Tu xoa co khi het loi */
    .int_polarity = ADT7410_POL_ACTIVE_LOW,
    .ct_polarity  = ADT7410_POL_ACTIVE_LOW,
    .fault_queue  = ADT7410_FAULT_QUEUE_2     /* 2 mau lien tiep vuot nguong */
};

status = adt7410_init(&sensor,
                      &iic_bus,
                      ADT7410_I2C_ADDR_A1L_A0L,
                      &init_cfg);
```

### Bước 3: Cài đặt ngưỡng cảnh báo nhiệt độ & độ trễ (`THIGH`, `TLOW`, `TCRIT`, `THYST`)

- **Ngưỡng nhiệt độ (`adt7410_set_temp_setpoint`)**: Đơn vị đầu vào là
  **milli-độ C (`mdegC`)** trong dải `-55000..+150000` (ví dụ `85000` tương ứng
  `+85.000 °C`, `-10000` tương ứng `-10.000 °C`). Driver tự làm tròn và mã hóa
  sang định dạng Q8.7 bù 2.
- **Độ trễ Hysteresis (`adt7410_set_hysteresis`)**: Đơn vị là **độ C nguyên
  (`degC`)** từ `0..15`, áp dụng chung cho `THIGH`, `TLOW` và `TCRIT`.

```c
status = adt7410_set_temp_setpoint(&sensor, ADT7410_SETPOINT_THIGH, 85000);
status = adt7410_set_temp_setpoint(&sensor, ADT7410_SETPOINT_TLOW, -10000);
status = adt7410_set_temp_setpoint(&sensor, ADT7410_SETPOINT_TCRIT, 105000);
status = adt7410_set_hysteresis(&sensor, 5U);
```

*(Tùy chọn)* Có thể đọc ngược lại bằng `adt7410_get_temp_setpoint()` và
`adt7410_get_hysteresis()` để xác nhận cấu hình trên phần cứng.

### Bước 4: Đọc nhiệt độ và cờ trạng thái (`adt7410_read_temp_degc`, `adt7410_read_temperature`, `adt7410_read_status`)

#### A. Cách 1 (Khuyến nghị cho tầng ứng dụng): Đọc trực tiếp ra `float` (`adt7410_read_temp_degc`)

Nếu tầng ứng dụng chỉ cần giá trị nhiệt độ theo **độ C (`float`)** mà không muốn
tự xử lý cấu trúc trung gian hay hệ số chia theo độ phân giải 13-bit / 16-bit,
gọi trực tiếp:

```c
float die_temp_degc = 0.0f;

status = adt7410_read_temp_degc(&sensor, &die_temp_degc);
```

- Driver tự động kiểm tra `sensor.active_res`:
  - Nếu đang ở **13-bit (`0.0625 °C/LSB`)**, driver tự động che (mask) bỏ 3 bit cờ
    `[2:0]` trước khi quy đổi sang độ C.
  - Nếu đang ở **16-bit (`0.0078125 °C/LSB`)**, driver lấy toàn bộ 16 bit.
- Phép nhân với `2^-7` (`0.0078125f`) là lũy thừa của 2 nên **hoàn toàn chính xác
  tuyệt đối trong chuẩn IEEE-754 `float`** (không có sai số làm tròn).

#### B. Cách 2: Đọc cấu trúc chi tiết số nguyên (`adt7410_read_temperature`) ở chế độ Continuous / 1 SPS

Gọi `adt7410_read_temperature(&sensor, &sample)` khi cần lấy đồng thời `mdegC`,
`Q8.7` và 3 cờ cảnh báo inline của chế độ 13-bit:
- `sample.die_temp_mdegc`: Nhiệt độ tính bằng milli-độ C (`1/1000 °C`).
- `sample.die_temp_q8_7`: Giá trị fixed-point `Q8.7` (`°C = die_temp_q8_7 / 128`).
- **Khác biệt giữa chế độ 13-bit và 16-bit**:
  - **Ở chế độ 13-bit (`ADT7410_RES_13BIT`)**: 3 bit thấp của thanh ghi nhiệt độ
    chứa cờ cảnh báo inline (`sample.has_inline_flags == true`), gồm
    `is_tcrit_active`, `is_thigh_active`, `is_tlow_active`.
  - **Ở chế độ 16-bit (`ADT7410_RES_16BIT`)**: Toàn bộ 16 bit dùng cho dữ liệu
    nhiệt độ nên `sample.has_inline_flags == false`. Để kiểm tra cờ cảnh báo
    hoặc trạng thái `RDY`, gọi thêm `adt7410_read_status(&sensor, &flags)`.

> **Lưu ý khi đọc thanh ghi Status (`0x02`)**: Ở chế độ
> `ADT7410_EVENT_INTERRUPT`, lệnh gọi `adt7410_read_status()` sẽ xóa các cờ
> chốt lỗi `TLOW`/`THIGH`/`TCRIT` (Datasheet tr. 14).

#### B. Chế độ One-Shot tiết kiệm năng lượng (`ADT7410_MODE_ONE_SHOT`)

1. Gọi `adt7410_trigger_one_shot(&sensor)`.
2. **Bắt buộc chờ tối thiểu `240 ms`** (theo Datasheet §ONE-SHOT MODE, tr. 10)
   hoặc kiểm tra `adt7410_read_status()` cho tới khi `flags.is_not_ready == false`.
3. Gọi `adt7410_read_temperature(&sensor, &sample)` để lấy mẫu.

### Bước 5: Đặt lại bằng phần mềm (`adt7410_soft_reset`) khi cần phục hồi

Nếu cần khôi phục cảm biến về mặc định phần cứng:
1. Gọi `adt7410_soft_reset(&sensor)` (gửi lệnh `0x2F`; driver tự cập nhật
   `active_res` về `ADT7410_RES_13BIT`).
2. **Bắt buộc chờ tối thiểu `200 µs`** (Datasheet §RESET, tr. 19) trước khi
   thực hiện giao dịch I2C tiếp theo.
3. Gọi lại `adt7410_set_config()` và cài lại các ngưỡng `setpoint`/`hysteresis`
   nếu ứng dụng không dùng cấu hình mặc định lúc cấp nguồn.

---

## 3. Bảng Xử Lý Mã Lỗi (`adt7410_status_t`)

| Mã trả về | Giá trị | Nguyên nhân | Hành động khuyến nghị ở tầng ứng dụng |
| :--- | :--- | :--- | :--- |
| `ADT7410_OK` | `0U` | Thành công | Sử dụng dữ liệu đầu ra |
| `ADT7410_ERR_PARAM` | `1U` | Con trỏ `NULL`, địa chỉ ngoài `0x48..0x4B`, cấu hình hoặc ngưỡng ngoài dải cho phép | Kiểm tra tham số truyền vào (lỗi lập trình/cấu hình) |
| `ADT7410_ERR_STATE` | `2U` | Chưa gọi `adt7410_init()` thành công, hoặc `init_state`/`active_res` bị lệch bit (SEU) | Gọi lại `adt7410_init()` để tái khởi tạo context và cảm biến |
| `ADT7410_ERR_I2C_COMM` | `3U` | Lỗi tầng HAL (`BUS_BUSY`, `NACK_ARB`, `TIMEOUT`, hoặc `iic_bus` chưa `READY`) | Kiểm tra kết nối bus, thử lại hoặc gọi lại `axi_iic_init()` + `adt7410_init()` |
| `ADT7410_ERR_DEV_ID` | `4U` | Manufacturer ID đọc từ thanh ghi `0x0B` khác `0x19` | Kiểm tra địa chỉ slave I2C và phần cứng cảm biến trên mạch |
| `ADT7410_ERR_NOT_READY` | `5U` | Dự phòng trong enum (xem `OI-02` trong `docs/OPEN_ITEMS.md`) | Chờ chu kỳ chuyển đổi tiếp theo |

---

## 4. Ví Dụ Tích Hợp Hoàn Chỉnh (Tuân Thủ C99 / MISRA / DO-178C)

```c
#include <stdbool.h>
#include <stdint.h>

#include "adt7410_driver.h"
#include "axi_iic_hal.h"

#define APP_AXI_IIC_BASE_ADDR   ((uintptr_t)0x40800000U)
#define APP_THIGH_LIMIT_MDEGC   (85000)   /* +85.000 degC */
#define APP_TLOW_LIMIT_MDEGC    (-10000)  /* -10.000 degC */
#define APP_TCRIT_LIMIT_MDEGC   (105000)  /* +105.000 degC */
#define APP_HYST_DEGC           (5U)      /* 5 degC */

static axi_iic_dev_t g_iic_bus;
static adt7410_dev_t g_temp_sensor;

adt7410_status_t app_temp_monitor_init(void)
{
    adt7410_status_t result = ADT7410_OK;
    axi_iic_status_t iic_status;
    adt7410_config_t const sensor_cfg =
    {
        .resolution   = ADT7410_RES_16BIT,
        .op_mode      = ADT7410_MODE_CONTINUOUS,
        .event_mode   = ADT7410_EVENT_COMPARATOR,
        .int_polarity = ADT7410_POL_ACTIVE_LOW,
        .ct_polarity  = ADT7410_POL_ACTIVE_LOW,
        .fault_queue  = ADT7410_FAULT_QUEUE_2
    };

    iic_status = axi_iic_init(&g_iic_bus,
                              APP_AXI_IIC_BASE_ADDR,
                              AXI_IIC_DEFAULT_POLLS);
    if (AXI_IIC_OK != iic_status)
    {
        result = ADT7410_ERR_I2C_COMM;
    }
    else
    {
        result = adt7410_init(&g_temp_sensor,
                              &g_iic_bus,
                              ADT7410_I2C_ADDR_A1L_A0L,
                              &sensor_cfg);
    }

    if (ADT7410_OK == result)
    {
        result = adt7410_set_temp_setpoint(&g_temp_sensor,
                                           ADT7410_SETPOINT_THIGH,
                                           APP_THIGH_LIMIT_MDEGC);
    }
    else
    {
        /* Keep prior error */
    }

    if (ADT7410_OK == result)
    {
        result = adt7410_set_temp_setpoint(&g_temp_sensor,
                                           ADT7410_SETPOINT_TLOW,
                                           APP_TLOW_LIMIT_MDEGC);
    }
    else
    {
        /* Keep prior error */
    }

    if (ADT7410_OK == result)
    {
        result = adt7410_set_temp_setpoint(&g_temp_sensor,
                                           ADT7410_SETPOINT_TCRIT,
                                           APP_TCRIT_LIMIT_MDEGC);
    }
    else
    {
        /* Keep prior error */
    }

    if (ADT7410_OK == result)
    {
        result = adt7410_set_hysteresis(&g_temp_sensor, APP_HYST_DEGC);
    }
    else
    {
        /* Keep prior error */
    }

    return result;
}

adt7410_status_t app_temp_monitor_poll(int32_t * p_temp_mdegc,
                                       bool * p_is_over_crit)
{
    adt7410_status_t result = ADT7410_OK;
    adt7410_sample_t sample;
    adt7410_status_flags_t flags;

    if ((NULL == p_temp_mdegc) || (NULL == p_is_over_crit))
    {
        result = ADT7410_ERR_PARAM;
    }
    else
    {
        result = adt7410_read_status(&g_temp_sensor, &flags);
        if (ADT7410_OK == result)
        {
            result = adt7410_read_temperature(&g_temp_sensor, &sample);
        }
        else
        {
            /* Status read failed */
        }

        if (ADT7410_OK == result)
        {
            *p_temp_mdegc  = sample.die_temp_mdegc;
            *p_is_over_crit = flags.has_tcrit_flt;
        }
        else
        {
            /* Do not modify outputs on error */
        }
    }

    return result;
}
```
