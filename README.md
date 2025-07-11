# GoldenChute
GoldenMate UPS monitor and Windows software  
  
This is a simple modification for the GoldeMate UPS to access the information displayed as well as safely shut a Windows PC down before the battery runs down. The thing that's important when using a UPS.  
  
![WebAndWinPic](GM_WebAndWin.png)  
  
The top is the web page for remote access, and the bottom is the Windows app.  

This mod requires opening the case of the UPS and plugging in a small board inline with the front display. It's a segmented display with SPI interface, so the ESP32 decodes all the bits and translates them to something usable. The data is only sent to the display when the power button is short-pressed, so this also simulates the button. The 5V power for the ESP32 needs to be external since the display 5V is only on when the display is powered, so run a USB cable to the rear panel and cut a small notch so the case can slide back on, and connect to PC or USB power supply (connected to the UPS, so it's powered on battery).  
  
Pics soon. I promise.
  
**Parts needed:**  
-  [PCB on OSHPark](https://oshpark.com/shared_projects/TjqZXsvM)  
-  ESP32-C3-super mini  
-  AQY282S (SSR)  
-  200 ohm 0805 resistor  
-  XBH 7P 2.54mm or 7P 0.1" pinheader M [all 4 on AliExpress](https://www.aliexpress.com/item/3256806815272828.html?spm=a2g0o.cart.0.0.59a838da1EfLxM&mp=1&pdp_npi=5%40dis%21USD%21USD%203.40%21USD%203.20%21%21USD%203.20%21%21%21%402101effb17521915717145693e67ce%2112000039009446985%21ct%21US%212963218209%21%211%210&_gl=1*11ywi8j*_gcl_dc*R0NMLjE3NTIxOTA3NTUuQ2p3S0NBand5YjNEQmhCbEVpd0FxWkxlNUoxOVp3bGNndjB2SXh4N3prNE1QM2pnOF9VN0RucFJGbnAxdDFMajE2RUM0SWNQN0tKamlCb0NDVm9RQXZEX0J3RQ..*_ga_VED1YSGNC7*czE3NTIxOTE1NzEkbzEkZzAkdDE3NTIxOTE1NzEkajYwJGwwJGgw)  
-  XBH 7P 2.54mm or 7P 0.1" pinheader F  
-  XBH 2P 2.54mm or 2P 0.1" connector M   
-  XBH 2P 2.54mm or 2P 0.1" connector F + 2 wires (2-3 inches) Note: The button is non-polarized  
  
**Windows app:**  
Extract the exe and move it to somewhere like C:\Goldenamte.  
Right click on the exe, and select "Show more Options" then select "Create shortcut" then move the shortcut to:  
  C:\Users\[Your Account]\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup  
Double-click to run the app.  Go to the tray, and right click the icon for the menu. Select "Settings" and enter the IP address of the ESP32 device in the format "192.168.xxx.xxx" which you can find in the Arduino IDE ports, named UPS (IP).  
If it connects, there should be a small red circle in the top left of the app. This will blink when data is recieved. It's just a filled circle for serial.  
Clicking on the top-right corner of the app will hide it.  
Other settings: COM Port (don't use if you have an IP address set. Note: using serial will cause the ESP32 to reset every time the app exits).  
Percent to shut down in 110% increments:  100% is immediate, and 0% is never. There is a 10 second delay when it reaches the desired %, then it will hibernate or hibrid-sleep if that is set up properly, otherwise it will shut down. The web page also allows manual remote shutdown/hibernate. Test it once to esnure it works properly.  
Alerts currently just cause the window to popup and show red text over the "Input" label, such as "Serial timeout" or WebSocket disconnected"  


Hide after start causes the window to hide 4 seconds after startup.  
The outage log (top right area, hidden when small) is blank until an entry is created.  Entries are comma delimited lines saved in a file in the local exe folder, named ups_log.txt.  

