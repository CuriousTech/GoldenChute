# GoldenChute
GoldenMate UPS monitor and Windows shutdown software  
  
This is a simple modification for the GoldeMate UPS to access the information displayed as well as safely shut a Windows PC down before the battery runs down. One of the most important features of a UPS.  
  
![WebAndWinPic](assets/GM_WebAndWin.png)  
  
The top is the web page for remote access, and the bottom is the Windows app.  
  
This mod requires opening the case of the UPS and plugging in a small board inline with the front display. It's a segmented display with SPI interface, so the ESP32 decodes all the segment bits and translates them to something usable. The data is only sent to the display for 60 seconds when the power button is short-pressed, so this also simulates the button (in parallel). The 5V power for the ESP32 needs to be external since the display 5V is only on when the display is powered, so run a USB cable to the rear panel and cut a small notch so the case can slide back on, and connect to PC or USB power supply (powered by the UPS, so it's powered on battery).  
  
I beleve the first model had a push-on/push-off button and the display stayed on. For that model, all you need is the PCB, ESP32, and 7 pin connectors. Leave out the solid state relay, R2 and 2 pin connectors.  
  
**Parts needed:**  
-  [PCB on OSHPark](https://oshpark.com/shared_projects/kwL7G532) Rev 2 on 8/23/25.  This is set up for HW SPI now, but not implemented yet. Rev 1 had bad issues. The BMS SPI is sensitive and 5V, so the ESP 3.3V zeners are probably interfering.  
-  [ESP32-C3-super mini](https://www.aliexpress.us/item/3256807353297685.html?spm=a2g0o.tesla.0.0.68bcQMQPQMQPhl&pdp_npi=5%40dis%21USD%21%242.70%21%240.99%21%21%21%21%21%40210318ec17532528750118282ee624%2112000041210885173%21btf%21%21%21%211%210&afTraceInfo=1005007539612437__pc__c_ppc_item_bridge_pc_jfy_wf__5EfmYTO__1753252875356&gatewayAdapt=glo2usa4itemAdapt)  
-  AQY282S (SSR)  
-  R2 470 ohm 0805 resistor  
-  R1 ??? ohm 0805 resistor (works if left off / ungrounded. This is odd! Will try 10K later, but leaving it off for now)  
-  M & F [JST-XH](https://www.aliexpress.us/item/3256806894018733.html?spm=a2g0o.productlist.main.4.6628yYL5yYL5tc&aem_p4p_detail=202507281307327593555654334650005149445&algo_pvid=4fba3b75-b535-414b-a67c-6606d6a9a4fe&algo_exp_id=4fba3b75-b535-414b-a67c-6606d6a9a4fe-3&pdp_ext_f=%7B%22order%22%3A%221150%22%2C%22eval%22%3A%221%22%7D&pdp_npi=4%40dis%21USD%211.69%210.99%21%21%211.69%210.99%21%402101c5ac17537332524376501e04f3%2112000039333381516%21sea%21US%212968017294%21ABX&curPageLogUid=1BpFPZiG5hsA&utparam-url=scene%3Asearch%7Cquery_from%3A&search_p4p_id=202507281307327593555654334650005149445_1) or [XHB](https://www.aliexpress.com/p/tesla-landing/index.html?UTABTest=aliabtest110188_5910&src=criteo&albch=criteo_New&acnt=criteo-B4&albcp=232508&device=pc&clickid=688081f42944a328ac413444d0863805_1753252340_3256806815272828&cto_pld=v5yJr7dcAABvALKgmy4wTg&aff_fcid=bcbb53245af5402988fd7376c89645a9-1753252356158-04892-UneMJZVf&aff_fsk=UneMJZVf&aff_platform=aaf&sk=UneMJZVf&aff_trace_key=bcbb53245af5402988fd7376c89645a9-1753252356158-04892-UneMJZVf&terminal_id=1a5a9f1087de44a890966b5bbd3921da&scenario=c_ppc_item_bridge&productId=3256806815272828&_immersiveMode=true&withMainCard=true&OLP=1094500108_f_group1&o_s_id=1094500108&afSmartRedirect=n) (clip type) 7P 2.54mm, or 7P 0.1" pinheader  
-  2P 2.54mm/0.1" connector M 
-  2P 2.54mm/0.1" connector F + 2 wires (2-3 inches) Note: The button is non-polarized (The AliExpress above didn't fit the 2 pin) Dupont will work if nothing else.  
  
![UPS Board](assets/ups.png)  
![Front panel](assets/front_panel.jpg)  
  
Remove the 2 screws under each rubber foot. Slide the cover off (to the front).  Remove the 6 screws around the front panel.  
  
The female XHB (XH works well, and a plain female pinheader actually holds well if you can't find XH/B) connector needs pins or wire lead clippings crimped to make it solder to a PCB. I used male pinheader pins. Install the 2-pin connector and wires before the ESP32 for ease.  
  
![Back](assets/notch.jpg)  
Cut a notch in the rear panel to fit the USB cable.  
  
**Arduino:**  
Settings for router SSID and SSID password are in Prefs.h, as well as the remote password for web control. If they aren't set or the SSID changes, EspTouch can be used.  
Compiling the code should be easy. Most information is in UPS.ino, like where to get the libraries needed, and settings for the ESP32-C3-super mini such as enabling CDC on boot (for serial output). There's also an ESP32-S3 super mini that is pin compatible.  
  
**Windows app:**  
Extract the exe and move it to somewhere like C:\Goldenamte.  
Right click on the exe, and select "Show more Options" then select "Create shortcut" then move the shortcut to:  
  C:\Users\\[Your Account]\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup  
Double-click to run the app.  
Clicking on the top-right corner of the app will hide it.  
Right-click for context menu (settings, quit, clear log/chart)  
The outage log (top right area, hidden when small) is blank until an entry is created.  Entries are comma delimited lines saved in a file in the local exe folder, named ups_log.txt.  
**Settings:**  
Right-click in the window, or go to the tray, and right click the icon for the menu. Select "Settings" and enter the IP address of the ESP32 device in the format "192.168.xxx.xxx" which you can find in the Arduino IDE ports, named UPS (192.168.xxx.xxx).  Your router should also be on UPS when using this method.  
If it connects, there should be a small red circle in the top left of the app. This will blink when data is received. Serial is the same.  
COM Port (Note: using serial will cause the ESP32 to reset every time the app exits or another app accesses the COM port, which loses WH history and may get off track if charging).  
The radio buttons allow selecting either COM or websocket.  
Percent to shut down: The % is just the bar levels, not actual percent (which could possibly be inaccurate). There is a 10 second delay when it reaches the desired %, then it will shut down or hibernate/hibrid-sleep if that is set up properly and doesn't fail, otherwise it will shut down.  
Note: Hibrid-sleep can wake immediately by odd USB devices if not set up properly. It should be tested before use.  
Skip seconds: 0 will add data to the chart every second (total 24 hours). 1 would be 48 hours, but miss every other second.  
The web page also allows manual remote shutdown/hibernate. The password will need to be the same here as in Prefs.h  Test it once to esnure it works properly.  
Alerts currently just cause the window to popup and show red text over the "Input" label, such as "Serial timeout/in use" or WebSocket disconnected"  
Hide after start causes the window to hide 4 seconds after startup.  
Battery health can be used to adjust the time remaining calculation. The time is based on the modeol ID (i.e. 1000VA) assuming a 90% efficient inverter (900Wh), current average watts used, and 100% everywhere else, so starting with 95~97% health should be close. The information from the display is 5 bars, but level 1 blinks to indicate 5-9% and 5 blinks to indicate 80-90%, so if it switches to backup while charging, the time could be a little low, but recalculates when the level changes. If the ESP is never reset, it should stay very close.  
  
