# ConsoleQuadra
Giocare in pixel art

## 📘 Descrizione

In questo video ti presento Console Quadra, un progetto open source ideato da Marco Prunca, ispirato dall'Oraquadra  2. 
Pur mantenendo la stessa filosofia, Console Quadra utilizza una piattaforma hardware leggermente potenziata che le permette di offrire funzioni 
avanzate su una matrice LED RGB 16×16 in puro pixel art style. Include orario, previsioni meteo, cronotermostato, orologi analogici e digitali, 
segnapunti, testi scorrevoli, giochi con suoni, e perfino l’interazione via web o con controller Bluetooth. Nel video ti mostro come costruirla 
passo dopo passo, spiegando ogni fase, dalla parte elettronica alla configurazione software, per poi personalizzarla come vuoi.

![IMG_7916](https://github.com/user-attachments/assets/1375530f-6408-4fbb-a370-21341af9e8ed)

Video tutorial qui: https://youtu.be/PtBYJcpI7i4 

---
## 🎛️ Materiali

* ESp32 32D: https://s.click.aliexpress.com/e/_c3zPrQhd
* USB 90° Standing bend G-M: https://s.click.aliexpress.com/e/_c38FrejD
* Sensore Gy-21: https://s.click.aliexpress.com/e/_c2JIw3x9
* Matrice LED 16x16: https://s.click.aliexpress.com/e/_c4Uqo53d
* Buzzer passivo (small): https://s.click.aliexpress.com/e/_c2RUBRFd
* Buzzer passivo (big): https://s.click.aliexpress.com/e/_c3rRNcmr
* Pulsanti 6x6x4.3mm: https://s.click.aliexpress.com/e/_c3YD3SC9
* Filamento ERYONE Galaxy Black: https://amzn.to/4rr35Zz
* Gamepad BT (opzionale): https://s.click.aliexpress.com/e/_c4cyPYh9

---

## 🎛️ Schema pratico di assemblaggio

![Screenshot 2026-04-01 alle 22 39 46](https://github.com/user-attachments/assets/f46885ac-e75d-4e6d-8685-7b20f7fc0c4c)

* Dispositivo da selezionare ESP32 Dev Module + Bluepad32 inserendo il link nella sezione File/Preferenze  https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json
* parametri da modificare nella sezione Strumenti/Partition scheme HUGE APP
* Collegare il dispositivo CONSOLE QUADRA con il WIFI e poi andare all'indirizzo 192.168.4.1 con un browser per impostare la connessione WIFI di casa vostra.
* Una volta connesso, andare all'inidirizzo IP mostrato mediante scorrimento in matrice per utilizzare il dispositivo.

---
# 📝 Revisioni

* 17/01/2026 V1.0 Versione iniziale.
* 19/01/2026 V1.1 Adattamento per ESP32-WROOM-32D (pin modificati per compatibilità)
---
## 🧾 Licenza

Questo progetto è distribuito con licenza
**Creative Commons – Attribuzione – Non Commerciale 4.0 Internazionale (CC BY-NC 4.0)**.

Puoi condividerlo e modificarlo liberamente, **citando l’autore**
(Davide Gatti / [survivalhacking](https://github.com/survivalhacking)) e **senza scopi commerciali**.

🔗 [https://creativecommons.org/licenses/by-nc/4.0/](https://creativecommons.org/licenses/by-nc/4.0/)

