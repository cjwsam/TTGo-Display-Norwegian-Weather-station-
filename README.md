# TTGO T-Display Weather Station  
# TTGO T-Display Værstasjon

A fun project that runs on the ESP32-based TTGO T-Display board. It fetches weather forecasts using the MET API, shows them on the onboard TFT display, and supports:

Et morsomt prosjekt som kjører på det ESP32-baserte TTGO T-Display-kortet. Det henter værmeldinger via MET API, viser dem på den integrerte TFT-skjermen, og støtter:

- **EEPROM** storage of WiFi credentials  
  **Lagring i EEPROM** av WiFi-opplysninger
- **WiFi fallback mode** with a **Captive Portal** for easy configuration  
  **WiFi-sikkerhetsmodus** med en **Captive Portal** for enkel konfigurasjon
- **Over-the-Air (OTA)** updates  
  **OTA-oppdateringer** (Over-the-Air)
- A simple **weather animation** on the main screen  
  En enkel **vær-animasjon** på hovedskjermen
- **Daily forecast pages**  
  **Daglige værmeldingssider**
- A **stats page** showing WiFi signal, heap memory, CPU frequency, etc.  
  En **statistikk-side** som viser WiFi-signal, heap-minne, CPU-frekvens, osv.

---

## Features / Funksjoner

1. **Weather Forecast / Værmelding**  
   - Fetches temperature and a symbolic forecast from the [MET Weather API](https://api.met.no).  
     Henter temperatur og en symbolsk værmelding fra [MET Weather API](https://api.met.no).  
   - Displays current conditions and upcoming daily summaries.  
     Viser gjeldende forhold og daglige væroversikter.

2. **WiFi Configuration / WiFi-konfigurasjon**  
   - If your board fails to connect to your stored WiFi credentials, it automatically starts an AP named `ESP32_AP` (with a default password) and launches a captive portal (at `192.168.4.1`).  
     Hvis kortet ikke klarer å koble til med lagrede WiFi-opplysninger, starter det automatisk et AP kalt `ESP32_AP` (med standardpassord) og åpner en captive portal (på `192.168.4.1`).  
   - You can configure your SSID and password directly in a simple web form.  
     Du kan konfigurere ditt SSID og passord direkte via et enkelt webskjema.

3. **OTA Updates / OTA-oppdateringer**  
   - Supports Arduino OTA. Configure your IDE or use `espota.py` to upload sketches wirelessly.  
     Støtter Arduino OTA. Konfigurer din IDE eller bruk `espota.py` for trådløs opplasting av skisser.

4. **TFT Display / TFT-skjerm**  
   - Uses the [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) library (ensure `User_Setup.h` is configured for TTGO T-Display).  
     Bruker biblioteket [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) (sørg for at `User_Setup.h` er konfigurert for TTGO T-Display).

---

## Hardware Requirements / Maskinvarekrav

- **TTGO T-Display ESP32** board (with ST7789 or similar TFT).  
  **TTGO T-Display ESP32**-kort (med ST7789 eller lignende TFT).  
- 2 onboard buttons for toggling the display and cycling pages.  
  2 integrerte knapper for å bytte visning og bla mellom sider.
- Recommended to use PlatformIO or Arduino IDE with the correct board settings.  
  Anbefalt å bruke PlatformIO eller Arduino IDE med riktige kortinnstillinger.

---

## Usage / Bruksanvisning

1. **Flash** the code onto your TTGO T-Display.  
   **Last opp** koden til din TTGO T-Display.
2. If you have pre-existing WiFi credentials in EEPROM, it tries to connect automatically.  
   Hvis du har lagret WiFi-opplysninger i EEPROM, forsøker den å koble til automatisk.
3. If it cannot connect:  
   Hvis den ikke klarer å koble til:
   - It starts its own Access Point called `ESP32_AP`.  
     Starter sitt eget Access Point kalt `ESP32_AP`.
   - Connect using a smartphone or laptop (password: `password123`).  
     Koble til med en smarttelefon eller bærbar PC (passord: `password123`).
   - A captive portal page at `192.168.4.1` will allow you to set your own WiFi credentials.  
     En captive portal-side på `192.168.4.1` lar deg sette dine egne WiFi-opplysninger.
4. After connecting successfully, it will get the time from NTP, fetch weather data, and display it.  
   Etter en vellykket tilkobling, hentes tiden via NTP, værdata lastes ned, og vises.

---

## Customization / Tilpasning

- To change the **weather location**, edit `weatherURL` in the code. Replace with your latitude and longitude.  
  For å endre **værsted**, rediger `weatherURL` i koden. Erstatt med din bredde- og lengdegrad.
- Adjust the **EEPROM_SIZE** constant if you need more or less space for stored credentials.  
  Juster konstanten **EEPROM_SIZE** hvis du trenger mer eller mindre plass for lagrede opplysninger.
- Modify the **animation** logic or **icon drawings** to suit your style.  
  Endre logikken for **animasjonen** eller **ikontegningene** for å tilpasse din stil.

---

## License / Lisens

This project is licensed under the [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.html).  
Dette prosjektet er lisensiert under [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.html).  
Author: **cjwsam**  
Forfatter: **cjwsam**
