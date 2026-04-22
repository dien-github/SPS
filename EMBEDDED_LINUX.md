### 1. Kiến trúc cốt lõi (Core Architecture)
*   **Mô hình thiết kế:** Kiến trúc đa tiến trình (Multi-process Architecture).
*   **Trục giao tiếp trung tâm (Message Bus):** `D-Bus IPC System` đóng vai trò là xương sống kết nối tất cả các tiến trình phần mềm độc lập thông qua cơ chế Publish/Subscribe và gọi hàm RPC. Các tiến trình không gọi trực tiếp vào bộ nhớ của nhau mà giao tiếp qua các cổng (Ports) cắm vào trục D-Bus này.

### 2. Đặc tả chi tiết các thành phần (Components Specification)

#### 2.1. Tầng Giao diện (User Interface Layer)
*   **Component:** `appHmiKiosk`
    *   **Vai trò:** Tiến trình Front-end (C++/QML) chịu trách nhiệm render giao diện toàn màn hình ở chế độ Kiosk, nhận thao tác chạm và giao tiếp với hệ thống qua D-Bus. Nó hoàn toàn không chứa logic xử lý phần cứng.
    *   **Provided Interface (Cung cấp):** `IUserEvents` (Phát tín hiệu về các hành vi thao tác của người dùng như bấm nút).
    *   **Required Interface (Yêu cầu):** `IUiController` (Tiếp nhận lệnh cập nhật trạng thái UI hoặc chuyển màn hình từ các dịch vụ nền).
    *   **External Connection (Ngoại vi):** Màn hình cảm ứng (Touch Display) kết nối thông qua plugin đồ họa EGLFS/HDMI/USB.

#### 2.2. Tầng Điều phối Trung tâm (Central Orchestration Layer)
*   **Component:** `svcAutomationEngine`
    *   **Vai trò:** Tiến trình Backend đóng vai trò "bộ não" xử lý kịch bản tự động hóa. Khi nhận được lệnh ngữ cảnh, nó tự động phân rã và điều phối các dịch vụ khác.
    *   **Provided Interface (Cung cấp):** `IAutomationLogic` (Mở API cho phép HMI hoặc các dịch vụ khác ra lệnh thực thi kịch bản cụ thể).
    *   **Required Interface (Yêu cầu):** `IAuthNotifier` (Lắng nghe kết quả quẹt thẻ), `IHardwareAPI` (Ra lệnh điều khiển MCU), `INetworkAPI` (Ra lệnh đánh thức PC), `IUserEvents` (Nghe tương tác thủ công từ HMI), `IUiController` (Ép HMI đổi giao diện).
    *   **External Connection:** Hoàn toàn xử lý logic ngầm, không kết nối trực tiếp với ngoại vi vật lý.

#### 2.3. Tầng Giao tiếp Ngoại vi (Peripheral Handlers Layer)
*   **Component:** `svcAuthentication`
    *   **Vai trò:** Tiến trình Backend quản lý việc xác thực cục bộ. Lắng nghe tín hiệu từ đầu đọc thẻ RFID, truy vấn cơ sở dữ liệu và cấp quyền mở khóa.
    *   **Provided Interface (Cung cấp):** `IAuthNotifier` (Phát tín hiệu broadcast `loginSuccess` kèm thông tin UID/Giảng viên lên hệ thống).
    *   **Required Interface:** Hoạt động độc lập, không phụ thuộc service khác.
    *   **External Connection:** Đầu đọc thẻ RFID nối trực tiếp vào Pi qua cổng USB/GPIO (`IRfidHardware`).

*   **Component:** `svcProtocolRouter`
    *   **Vai trò:** Tiến trình Backend đóng vai trò cầu nối biên dịch và giao tiếp liên tục với vi điều khiển (MCU) qua cổng UART.
    *   **Provided Interface (Cung cấp):** `IHardwareAPI` (Cung cấp hàm điều khiển thiết bị nguyên thủy), `IHardwareStatus` (Báo cáo trạng thái phản hồi từ MCU).
    *   **Required Interface (Yêu cầu):** `IHardwareNotifier` (Phát tín hiệu báo lỗi mạch MCU lên D-Bus).
    *   **External Connection:** Vi điều khiển MCU (STM32) qua cổng UART phần cứng (`ISerialLink`).

#### 2.4. Tầng Mạng và Bảo trì (Network & Maintenance Layer)
*   **Component:** `svcNetworkManager`
    *   **Vai trò:** Tiến trình Backend quản lý kết nối mạng LAN/Wi-Fi, nhận thông tin đồng bộ dữ liệu giảng viên (JSON) từ Server, đẩy log qua MQTT và thực hiện Wake-on-LAN để đánh thức PC.
    *   **Provided Interface (Cung cấp):** `INetworkAPI` (Cung cấp hàm nhận lệnh WoL, đồng bộ DB), `IConnStatus` (Phát tín hiệu trạng thái kết nối mạng).
    *   **Required Interface:** Không có.
    *   **External Connection:** Switch/Router nội bộ và Server thông qua Ethernet/Internet.

*   **Component:** `svcOtaManager`
    *   **Vai trò:** Tiến trình Backend tiếp nhận dữ liệu cập nhật từ Server, tải payload và thực hiện cập nhật ứng dụng Linux hoặc nạp firmware mới cho MCU.
    *   **Provided Interface (Cung cấp):** `IOtaStatus` (Phát tín hiệu thông báo tiến độ cập nhật).
    *   **Required Interface (Yêu cầu):** `IUiController` (Ép màn hình HMI khóa tương tác khi đang cập nhật), `IHardwareAPI` (Gọi UART để truyền file firmware nhị phân xuống MCU).
    *   **External Connection:** Máy chủ lưu trữ bản cập nhật (OTA Payload Server) qua giao thức HTTP/HTTPS (`ICloudServer`).

### 3. Thành phần Dữ liệu Cục bộ (Local Data Component)
*   **Component:** `Local_DB.sqlite`
    *   **Vai trò:** Cơ sở dữ liệu nội bộ lưu trữ danh sách thẻ RFID, thông tin giảng viên và lịch biểu.
    *   **Tính chất kỹ thuật:** Tệp cơ sở dữ liệu này được cấu hình ở chế độ `WAL` (Write-Ahead Logging) để cho phép tiến trình `svcNetworkManager` ghi đè dữ liệu đồng bộ trong khi tiến trình `svcAuthentication` vẫn có thể đọc dữ liệu quét thẻ cùng lúc mà không gây lỗi crash (Database Corruption).