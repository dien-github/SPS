# SPS - Smart Podium System

SPS, viết tắt của Smart Podium System, là hệ thống bục giảng thông minh dùng để xác thực giảng viên, điều khiển thiết bị trong phòng học và cho phép IT Admin giám sát hoặc điều khiển từ xa qua server nội bộ của trường.

Mục tiêu thiết kế là một hệ thống offline-first: khi mất kết nối tới server, thiết bị đặt tại bục giảng vẫn xác thực thẻ RFID và điều khiển các thiết bị trong phòng học bình thường.

## 1. Bức tranh tổng thể

Một phòng học có một PCD, tức Podium Control Device. PCD là hệ thống lai gồm Raspberry Pi chạy Linux và STM32 làm bộ điều khiển thời gian thực.

```text
                         Local Server của trường
                 Docker: Node API + PostgreSQL + Mosquitto
                         | MQTT: telemetry/commands
                         | HTTP: OTA packages
                         v
+----------------------------------------------------------------+
| PCD - Podium Control Device                                    |
|                                                                |
|  Raspberry Pi / Embedded Linux / Qt6                           |
|  +----------------------------------------------------------+  |
|  | appHmi                                                  |  |
|  | Qt/QML kiosk UI trên màn hình cảm ứng                   |  |
|  +----------------------------------------------------------+  |
|              | D-Bus system bus                               |
|  +-----------+------------+------------+------------+-------+  |
|  | svcAuthentication     svcAutoEngine  svcNetworkManager   |  |
|  | RFID + lecturer DB    scenario FSM   MQTT + WoL          |  |
|  |                                                           |  |
|  | svcProtocolRouter     svcOtaManager                      |  |
|  | UART frame to MCU     dual OTA orchestration             |  |
|  +-----------+-----------------------------------------------+  |
|              | UART: 0xAA 0x55 Len Cmd Payload CRC16           |
+--------------+-------------------------------------------------+
               v
+----------------------------------------------------------------+
| STM32 MCU firmware                                             |
| Parse UART frame, dispatch command, control relay, report state |
+----------------------------------------------------------------+
               v
    Đèn, rèm, màn chiếu, máy chiếu, điều hòa, máy tính trạm
```

Các vai trò chính:

| Thành phần | Chạy ở đâu | Vai trò |
| --- | --- | --- |
| SBC/Raspberry Pi | PCD | Chạy HMI, D-Bus services, mạng, OTA, dữ liệu cục bộ |
| STM32 MCU | PCD | Điều khiển relay/cảm biến theo thời gian thực |
| Local Server | Server trường | Quản lý phòng học, thiết bị, lecturer DB, MQTT, OTA |
| Màn hình cảm ứng | PCD | Giao diện kiosk cho giảng viên |
| RFID reader | Gắn vào Pi | Xác thực giảng viên tại biên |

## 2. Tổ chức mã nguồn

```text
.
|-- firmware/                         # Code/contract cho STM32
|   |-- sps_protocol.h                # Frame UART, parser API, ACK/NACK
|   |-- command_dispatcher.h          # Dispatch frame sang logic firmware
|   |-- relay_service.h               # Mapping relay/device/action/status
|   |-- main.h, gpio.h, usart.h       # STM32 HAL/LL headers
|
|-- linuxapp/                         # Ứng dụng chạy trên Raspberry Pi
|   |-- CMakeLists.txt                # Build Qt6/C++17
|   |-- configs/buildroot/            # Buildroot defconfig cho image nhúng
|   |-- scripts/setup_dev_env.sh      # Cài môi trường dev native
|   |-- src/
|       |-- common/                   # Logger, constants, models, service base
|       |-- interfaces/               # D-Bus XML contracts
|       |-- apps/appHmi/              # Qt/QML kiosk app
|       |-- services/
|           |-- svcAuthentication/    # RFID + xác thực lecturer
|           |-- svcAutoEngine/        # Scenario/state machine
|           |-- svcProtocolRouter/    # UART protocol tới STM32
|           |-- svcNetworkManager/    # MQTT, network, Wake-on-LAN
|           |-- svcOtaManager/        # OTA cho MCU firmware và Linux app
|
|-- server/                           # Backend server nội bộ
|   |-- docker-compose.yml            # PostgreSQL + Mosquitto + API
|   |-- src/server.js                 # Express API hiện có health check
|   |-- mosquitto/mosquitto.conf      # MQTT broker config
|
|-- tests/
|   |-- integration/sps_integration_tester.py
|                                      # CLI test D-Bus, MQTT, UART scenarios
```

## 3. Kiến trúc phần mềm trên Raspberry Pi

`linuxapp/` là phần trung tâm của runtime. Hệ thống dùng kiến trúc đa tiến trình, mỗi service là một executable riêng, giao tiếp qua D-Bus system bus. Cách tách này giúp HMI không bị treo theo khi một service phần cứng hoặc mạng gặp lỗi.

| Process | D-Bus service | Target CMake | Trách nhiệm |
| --- | --- | --- | --- |
| `appHmi` | D-Bus client | `appHmi` | Render Qt/QML fullscreen kiosk, gọi service qua D-Bus |
| `svcAuthentication` | `com.sps.auth` | `svcAuthentication` | Đọc RFID, xác thực lecturer, phát trạng thái khóa/mở |
| `svcAutoEngine` | `com.sps.engine` | `svcAutoEngine` | Chạy scenario, điều phối lệnh thiết bị qua router |
| `svcProtocolRouter` | `com.sps.router` | `svcProtocolRouter` | Đóng gói/parse UART frame, retry, cache trạng thái thiết bị, OTA chunk |
| `svcNetworkManager` | `com.sps.netmgr` | `svcNetworkManager` | MQTT pub/sub, heartbeat, network status, Wake-on-LAN |
| `svcOtaManager` | `com.sps.otamanager` | `svcOtaManager` | Tải gói OTA, verify checksum, flash MCU qua router, cập nhật binary service |

Các file nền dùng chung:

| File | Ý nghĩa |
| --- | --- |
| `linuxapp/src/common/sps_service_base.*` | Base class đăng ký service/object lên D-Bus và quản lý lifecycle |
| `linuxapp/src/common/sps_constants.h` | Tên service/path/interface, UART constants, OTA constants |
| `linuxapp/src/common/sps_device_models.h` | Model `Device`, `Room`, `Lecturer`, `Scenario`, `UartFrame`, `MqttMessage` |
| `linuxapp/src/common/sps_logger.h` | Logger singleton ghi log theo component |
| `linuxapp/src/interfaces/*.xml` | Hợp đồng D-Bus, từ đó Qt sinh adaptor/proxy bằng CMake |

## 4. Các luồng xử lý chính

### RFID mở khóa bục giảng

```text
Lecturer quẹt thẻ
-> svcAuthentication đọc RFID
-> kiểm tra lecturer database cục bộ
-> phát AuthStatusChanged/LecturerAuthenticated qua D-Bus
-> appHmi chuyển từ lock screen sang dashboard
```

Trong code hiện tại, `svcAuthentication` đọc `/opt/sps/config/lecturers.json` và có dữ liệu demo fallback khi file chưa tồn tại. Thiết kế mục tiêu của dự án là chuyển lớp dữ liệu cục bộ này sang SQLite ở chế độ WAL để vẫn đọc được khi service đồng bộ dữ liệu đang ghi.

### Giảng viên điều khiển thiết bị tại phòng

```text
appHmi
-> svcAutoEngine ExecuteScenario hoặc ControlDevice
-> svcProtocolRouter SendCommand
-> UART frame sang STM32
-> MCU điều khiển relay/cảm biến
-> ACK/NACK hoặc status quay lại qua UART
-> D-Bus signal cập nhật HMI/service khác
```

`svcAutoEngine` hiện đã load scenario từ `/opt/sps/config/scenarios.json` và có scenario demo fallback. Lệnh cuối cùng được map sang UART command như `LIGHT_CONTROL`, `PROJECTOR_CONTROL`, `AC_CONTROL`.

### IT Admin điều khiển từ server

```text
Dashboard/API server
-> MQTT topic sps/{room}/cmd/...
-> svcNetworkManager nhận message
-> phát command nội bộ hoặc gọi service liên quan
-> router/engine điều khiển thiết bị
-> publish trạng thái/event ngược lại MQTT
```

MQTT topic đang được chuẩn hóa trong `mqtt_defines.h`, ví dụ:

| Topic | Ý nghĩa |
| --- | --- |
| `sps/{RoomID}/status/devices` | Snapshot trạng thái thiết bị |
| `sps/{RoomID}/status/connection` | Heartbeat/LWT kết nối |
| `sps/{RoomID}/event/auth` | Sự kiện xác thực |
| `sps/{RoomID}/event/ota` | Tiến độ OTA |
| `sps/{RoomID}/cmd/projector` | Lệnh máy chiếu |
| `sps/{RoomID}/cmd/relay` | Lệnh relay/đèn/rèm |
| `sps/{RoomID}/cmd/ac` | Lệnh điều hòa |
| `sps/{RoomID}/cmd/sync` | Đồng bộ lecturer/config |
| `sps/{RoomID}/cmd/ota` | Kích hoạt OTA |

### Dual OTA

```text
Server gửi URL + checksum
-> svcOtaManager tải package bằng HTTP
-> verify SHA-256 nếu có checksum
-> nếu là MCU firmware: gọi svcProtocolRouter StartOTA/SendOTAChunk/EndOTA
-> nếu là Linux app: stop service, backup binary, replace binary, start service
-> phát UpdateProgress/UpdateCompleted qua D-Bus
```

Các binary runtime mục tiêu nằm ở `/opt/sps/bin`, file tải tạm ở `/opt/sps/updates`, version ở `/opt/sps/config/version.json`.

## 5. Giao thức nội bộ

### D-Bus

D-Bus là xương sống IPC giữa các tiến trình trên Pi. Các interface chính nằm trong `linuxapp/src/interfaces/`:

| Interface | Method/signal tiêu biểu |
| --- | --- |
| `com.sps.auth` | `GetAuthStatus`, `UnlockScreen`, `LockScreen`, `LecturerAuthenticated` |
| `com.sps.engine` | `ExecuteScenario`, `GetAvailableScenarios`, `ControlDevice`, `ScenarioCompleted` |
| `com.sps.router` | `SendCommand`, `GetDeviceStatus`, `StartOTA`, `OTAProgress` |
| `com.sps.netmgr` | `PublishEvent`, `SendWakeOnLAN`, `SyncLecturerList`, `CommandReceived` |
| `com.sps.otamanager` | `StartMcuFirmwareUpdate`, `StartAppServiceUpdate`, `StartFullUpdate` |

### UART SBC-MCU

Frame UART giữa Raspberry Pi và STM32:

```text
[0xAA][0x55][Length][CmdId][Payload...][CRC16 high][CRC16 low]
```

Thông số hiện tại:

| Thuộc tính | Giá trị |
| --- | --- |
| Baudrate | `115200` |
| Port mặc định trên Pi | `/dev/ttyS0` |
| CRC | CRC-16/CCITT |
| Payload tối đa phía linuxapp | `256` bytes |
| OTA chunk | `128` bytes |

Command ID chính:

| Command | Hex | Ý nghĩa |
| --- | --- | --- |
| `PING_HEARTBEAT` | `0x10` | Kiểm tra MCU còn sống |
| `ACK_ALIVE` | `0x11` | MCU ACK command |
| `NACK_ERROR` | `0x12` | MCU báo lỗi |
| `LIGHT_CONTROL` | `0x21` | Điều khiển đèn/relay |
| `CURTAIN_CONTROL` | `0x22` | Điều khiển rèm/màn |
| `PROJECTOR_CONTROL` | `0x23` | Điều khiển máy chiếu |
| `AC_CONTROL` | `0x24` | Điều khiển điều hòa |
| `QUERY_RELAY_STATUS` | `0x32` | Đọc trạng thái relay |
| `PRESENCE_ALERT` | `0x41` | Sự kiện cảm biến hiện diện |
| `OTA_START`, `OTA_DATA_CHUNK`, `OTA_END` | `0x50`-`0x52` | Nạp firmware MCU |

## 6. Backend server

`server/` là stack chạy trên server nội bộ của trường:

| Container | Công nghệ | Vai trò |
| --- | --- | --- |
| `sps-db` | PostgreSQL 16 | Master DB cho lecturer, room, device, event |
| `sps-mqtt` | Eclipse Mosquitto | Broker MQTT cho telemetry/command |
| `sps-api` | Node.js/Express | REST API và điểm phát OTA package |

Hiện trạng source server đang có endpoint `/health` và hạ tầng Docker Compose. Các phần dashboard, schema DB, ORM/Prisma, worker MQTT-to-DB và API nghiệp vụ là phần kiến trúc mục tiêu cần tiếp tục triển khai.

## 7. Firmware STM32

`firmware/` chứa contract và header cho firmware STM32F4:

| File | Vai trò |
| --- | --- |
| `sps_protocol.h` | Parser state machine, frame format, ACK/NACK, error code |
| `command_dispatcher.h` | API nhận frame đã parse để dispatch command |
| `relay_service.h` | Device/action/status cho relay đèn, rèm, màn |
| `main.h`, `gpio.h`, `usart.h` | STM32 HAL/LL pin và peripheral declarations |
| `FreeRTOSConfig.h` | Cấu hình FreeRTOS |

Firmware nhận frame từ `svcProtocolRouter`, thực thi trên relay/cảm biến, rồi gửi ACK/NACK hoặc trạng thái ngược lại.

## 8. Build và chạy nhanh

### Linux app native build

Trên Ubuntu/Debian:

```bash
cd linuxapp
./scripts/setup_dev_env.sh --build
```

Hoặc build thủ công:

```bash
cmake -S linuxapp -B linuxapp/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build linuxapp/build --parallel
```

Các executable được sinh vào `linuxapp/build/bin/`.

### Server

```bash
cd server
docker compose up --build
```

Sau khi chạy:

| Service | Port |
| --- | --- |
| API | `3000` |
| PostgreSQL | `5432` |
| MQTT | `1883` |

Health check:

```bash
curl http://localhost:3000/health
```

### Integration test CLI

```bash
pip install -r tests/requirements.txt
python tests/integration/sps_integration_tester.py list
python tests/integration/sps_integration_tester.py run TC-01
python tests/integration/sps_integration_tester.py run --all
```

Các test này giả lập/kiểm tra các luồng D-Bus, MQTT và UART ở mức tích hợp. Muốn test pass đầy đủ cần các service SPS đang chạy trên D-Bus system bus.

## 9. Hiện trạng quan trọng trong mã nguồn

Những điểm đã có:

- CMake build cho 6 executable chính của `linuxapp/`.
- D-Bus XML interface cho auth, engine, router, network, OTA.
- UART frame builder/parser với CRC-16, receive buffer và retry command queue.
- Qt/QML HMI fullscreen kiosk, có màn hình khóa và dashboard điều khiển cơ bản.
- OTA manager có flow tải file, checksum, flash MCU qua router và thay binary service.
- Docker Compose cho PostgreSQL, Mosquitto và Node API.
- Integration tester CLI mô tả các kịch bản end-to-end.

Những điểm còn là scaffold hoặc cần hoàn thiện:

- `svcAuthentication` đang dùng JSON demo thay vì SQLite WAL.
- `MqttClient` hiện là wrapper mô phỏng, chưa nối thư viện MQTT thật.
- `server/src/server.js` mới có `/health`, chưa có API/dashboard/worker nghiệp vụ.
- `appHmi` còn nút demo login và một số lệnh thiết bị trực tiếp chưa map đầy đủ.
- `firmware/` hiện chủ yếu là header/contract, chưa thấy đầy đủ source `.c` trong repo.
- Buildroot config hiện có tên `sps_pi_3_64_defconfig`; nếu target cuối là Raspberry Pi 4 cần kiểm tra lại defconfig, kernel DTS và firmware package tương ứng.

## 10. Quy ước làm việc nhóm

- Tính năng mới nên phát triển trên nhánh riêng, ví dụ `feature/ir_task`.
- Khi code đã build được và không còn lỗi vặt, tạo merge request vào nhánh `develop`.
- Khi các phần web, firmware, linux app và docs ổn định, merge `develop` vào `main` để release.
