# GoldenChute
GoldenMate UPS monitor and Windows software  
  
This is a simple modification for the GoldeMate UPS to access the information displayed as well as safely shut a Windows PC down before the battery runs down. The thing that's important when using a UPS.  
  
![WebAndWinPic](GM_WebAndWin.png)  
  
The top is the web page for remote access, and the bottom is the Windows app.  

This mod requires opening the case of the UPS and plugging in a small board inline with the front display. It's a segmented display with SPI interface, so the ESP32 decodes all the bits and translates them to something usable. The data is only sent to the display when the power button is short-pressed, so this also simulates the button (in parallel). The 5V power for the ESP32 needs to be external since the display 5V is only on when the display is powered, so run a USB cable to the rear panel and cut a small notch so the case can slide back on, and connect to PC or USB power supply (connected to the UPS, so it's powered on battery).  
  
Pics soon. I promise.
  
I beleve the first model had a push-on/push-off button and the display satayed on. For that model, all you need is the PCB, ESP32, and 7 pin connectors. Leave out the solid state relay, resistor and 2 pin connectors.  
  
**Parts needed:**  
-  [PCB on OSHPark](https://oshpark.com/shared_projects/TjqZXsvM)  
-  [ESP32-C3-super mini](https://www.aliexpress.us/item/3256807353297685.html?spm=a2g0o.tesla.0.0.68bcQMQPQMQPhl&pdp_npi=5%40dis%21USD%21%242.70%21%240.99%21%21%21%21%21%40210318ec17532528750118282ee624%2112000041210885173%21btf%21%21%21%211%210&afTraceInfo=1005007539612437__pc__c_ppc_item_bridge_pc_jfy_wf__5EfmYTO__1753252875356&gatewayAdapt=glo2usa4itemAdapt)  
-  AQY282S (SSR)  
-  1.2K ohm 0805 resistor  
-  M & F JST-XH or [XHB](https://www.aliexpress.com/p/tesla-landing/index.html?UTABTest=aliabtest110188_5910&src=criteo&albch=criteo_New&acnt=criteo-B4&albcp=232508&device=pc&clickid=688081f42944a328ac413444d0863805_1753252340_3256806815272828&cto_pld=v5yJr7dcAABvALKgmy4wTg&aff_fcid=bcbb53245af5402988fd7376c89645a9-1753252356158-04892-UneMJZVf&aff_fsk=UneMJZVf&aff_platform=aaf&sk=UneMJZVf&aff_trace_key=bcbb53245af5402988fd7376c89645a9-1753252356158-04892-UneMJZVf&terminal_id=1a5a9f1087de44a890966b5bbd3921da&scenario=c_ppc_item_bridge&productId=3256806815272828&_immersiveMode=true&withMainCard=true&OLP=1094500108_f_group1&o_s_id=1094500108&afSmartRedirect=n) (clip type) 7P 2.54mm, or 7P 0.1" pinheader  
-  2P 0.1" connector M 
-  2P 0.1" connector F + 2 wires (2-3 inches) Note: The button is non-polarized  (The AliExpress above didn't fit the 2 pin)  (Dupont will work if nothing else)  

Settings for router SSID and SSID password are in Prefs.h, as well as the remote password for web control. If they aren't set or the SSID changes, EspTouch can be used.  
  
**Windows app:**  
Extract the exe and move it to somewhere like C:\Goldenamte.  
Right click on the exe, and select "Show more Options" then select "Create shortcut" then move the shortcut to:  
  C:\Users\[Your Account]\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup  
Double-click to run the app.  Go to the tray, and right click the icon for the menu. Select "Settings" and enter the IP address of the ESP32 device in the format "192.168.xxx.xxx" which you can find in the Arduino IDE ports, named UPS (IP).  
If it connects, there should be a small red circle in the top left of the app. This will blink when data is recieved. It's just a filled circle for serial.  
Clicking on the top-right corner of the app will hide it.  
Other settings: COM Port (Note: using serial will cause the ESP32 to reset every time the app exits).  
The radio buttons allow selecting either COM or websocket.  
Percent to shut down in 10% increments:  100% is immediate, and 0% is never. There is a 10 second delay when it reaches the desired %, then it will hibernate or hibrid-sleep if that is set up properly, otherwise it will shut down. The web page also allows manual remote shutdown/hibernate. The password will need to be the same here as in Prefs.h  Test it once to esnure it works properly.  
Alerts currently just cause the window to popup and show red text over the "Input" label, such as "Serial timeout" or WebSocket disconnected"  
  
Hide after start causes the window to hide 4 seconds after startup.  
The outage log (top right area, hidden when small) is blank until an entry is created.  Entries are comma delimited lines saved in a file in the local exe folder, named ups_log.txt.  

