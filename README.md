# TTGO T-Display Weather Station

A fun project that runs on the ESP32-based TTGO T-Display board. It fetches weather forecasts using the MET API, shows them on the onboard TFT display, and supports:

- **EEPROM** storage of WiFi credentials  
- **WiFi fallback mode** with a **Captive Portal** for easy configuration  
- **Over-the-Air (OTA)** updates  
- A simple **weather animation** on the main screen  
- **Daily forecast pages**  
- A **stats page** showing WiFi signal, heap memory, CPU frequency, etc.

## Features

1. **Weather Forecast**  
   - Fetches temperature and a symbolic forecast from the [MET Weather API](https://api.met.no).  
   - Displays current conditions and upcoming daily summaries.

2. **WiFi Configuration**  
   - If your board fails to connect to your stored WiFi credentials, it automatically starts an AP named `ESP32_AP` (with a default password) and launches a captive portal (at `192.168.4.1`).  
   - You can configure your SSID and password directly in a simple web form.

3. **OTA Updates**  
   - Supports Arduino OTA. Configure your IDE or use `espota.py` to upload sketches wirelessly.

4. **TFT Display**  
   - Uses the [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) library (ensure `User_Setup.h` is configured for TTGO T-Display).

## Hardware Requirements

- **TTGO T-Display ESP32** board (with ST7789 or similar TFT).  
- 2 onboard buttons for toggling the display and cycling pages.  
- Recommended to use PlatformIO or Arduino IDE with the correct board settings.

## Usage

1. **Flash** the code onto your TTGO T-Display.
2. If you have pre-existing WiFi credentials in EEPROM, it tries to connect automatically.
3. If it cannot connect:
   - It starts its own Access Point called `ESP32_AP`.
   - Connect using a smartphone or laptop (password: `password123`).
   - A captive portal page at `192.168.4.1` will allow you to set your own WiFi credentials.
4. After connecting successfully, it will get the time from NTP, fetch weather data, and display it.

## Customization

- To change the **weather location**, edit `weatherURL` in the code. Replace with your latitude and longitude.
- Adjust the **EEPROM_SIZE** constant if you need more or less space for stored credentials.
- Modify the **animation** logic or **icon drawings** to suit your style.

## License

This project is licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.html).  
Author: **cjwsam**  

