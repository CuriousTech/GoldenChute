const char page_index[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>GoldenMate UPS</title>
<style>
div,table{
border-radius: 2px;
margin-bottom: 2px;
box-shadow: 4px 4px 10px #000000;
background: rgb(160,160,160);
background: linear-gradient(0deg, rgba(94,94,94,1) 0%, rgba(160,160,160,1) 90%);
background-clip: padding-box;
}
input{
border-radius: 2px;
margin-bottom: 2px;
box-shadow: 4px 4px 10px #000000;
background: rgb(160,160,160);
background: linear-gradient(0deg, rgba(160,160,160,1) 0%, rgba(239,255,255,1) 100%);
background-clip: padding-box;
}
body{width:490px;display:block;text-align:right;font-family: Arial, Helvetica, sans-serif;}
</style>
<script src="http://ajax.googleapis.com/ajax/libs/jquery/1.6.1/jquery.min.js" type="text/javascript" charset="utf-8"></script>
<script type="text/javascript">
a=document.all
noData=0
bPwr=0
errorTxt=[
  "",
  "Low Voltage", // 1
  "High Voltage",
  "Short Circuit",
  "Temperature",
  "Overload", // 5
  "Communication",
  "Fan",
  "High Output Voltage", // differing manuals
  "High Output Voltage" // 9 " "
]

function openSocket(){
 ws=new WebSocket("ws://"+window.location.host+"/ws")
//  ws=new WebSocket("ws://192.168.31.155/ws")
  date = new Date()
  ws.onopen=function(evt){}
  ws.onclose=function(evt){alert("Connection closed");}
  ws.onmessage=function(evt){
  console.log(evt.data)
  d=JSON.parse(evt.data)
  switch(d.cmd)
  {
    case 'state':
      dt=new Date(d.t*1000)
      a.topbar.innerHTML=((+d.connected)?'PC Connected ':'PC Disonnected')+'&nbsp; '+dt.toLocaleTimeString()+' &nbsp;'+d.rssi+'dB'
      dtc=new Date(d.cycledate*1000)
      a.cycle.innerHTML=((+d.nc)?'<span style=\"color: red;\">':'')+dtc.getFullYear()+'/'+(dtc.getMonth()+1)+'/'+dtc.getDate()
      a.cycles.innerHTML=d.cycles+' &nbsp; +'+d.percuse+'%'
      a.health.innerHTML=d.health+'%'
      noData=+d.nodata
      a.ppkwh.value=ppkwh=+d.ppkwh/100
      a.WCL.value=+d.wcl
      a.RCL.value=+d.rcl
      wmin=d.wmin
      wmax=d.wmax
      tot=0
      drawstuff()
      break
    case 'alert':
      alert(d.text)
      break
    case 'data':
      dt=new Date(d.t*1000)
      a.topbar.innerHTML=((+d.connected)?'PC Connected ':'PC Disonnected')+'&nbsp; '+dt.toLocaleTimeString()+' &nbsp;'+d.rssi+'dB'
      upsState=d
      noData=+d.nodata
      draw()
      break
  }
 }
}

function setVar(varName, value)
{
  ws.send('{"key":"'+a.myKey.value+'","'+varName+'":'+value+'}')
}

function shutdown()
{
  setVar('shutdown',0)
}
function hibernate()
{
  setVar('hibernate',0)
}

function confirmPwr()
{
  if(confirm('Are you sure you want to turn the UPS off?'))
    setVar('power',0xABC20003)
}

function draw(){
  graph = $('#meter')
  c=graph[0].getContext('2d')

  c.fillStyle='black'
  yPad=3
  c.lineWidth=2
  c.fillRect(0, 0, graph.width(), graph.height())
  c.fillStyle=c.strokeStyle='white'

  if(+upsState.error)
  {
    c.font='20pt sans-serif'
    c.textAlign="left"
    c.textBaseline="middle"
    c.fillText('ERROR', 14, 20)
    c.fillText('U'+upsState.WattsIn, 14, 55)
    c.fillText(errorTxt[Math.floor(upsState.WattsIn/10)], 14, 88)
    return
  }
  if(noData)
  {
    c.font='20pt sans-serif'
    c.textAlign="left"
    c.textBaseline="middle"
    c.fillText('NO DISPLAY DATA', 14, 20)
    return
  }
  c.textAlign="right"
  c.textBaseline="middle"

  c.font='bold 10pt sans-serif'
  c.fillStyle='rgb(255,30,30)'
  if(+upsState.UPS)
    c.fillText("UPS", 268, 10)
  if(+upsState.AC)
    c.fillText("AC", 80, 10)

  c.font='10pt sans-serif'
  c.strokeStyle=c.fillStyle='rgb(210,255,255)'
  c.fillText("Input", 48, 10)
  c.fillText(upsState.battPercent+'%', 156, 10)
  c.fillText("Output", 230, 10)
  c.textAlign="center"
  c.fillText(timeRem(+d.secsrem), 138, 27)
  c.textAlign="right"

  c.font='italic 22pt sans-serif'
  if(upsState.voltsIn)
    c.fillText(upsState.voltsIn+'v', 84, 46)
  c.fillText(upsState.voltsOut+'v', 256, 46)
  c.fillText(upsState.wattsIn+'w', 84, 86)
  c.fillText(upsState.wattsOut+'w', 256, 86)

  c.fillStyle=c.strokeStyle='rgb(0,120,255)'
  c.roundRect(114,36,48,66,3)
  c.stroke()
  y = 88
  for(i = 0; i < 5; i++)
  {
    if(i >= upsState.BATT)
      c.strokeRect(118+1, y+1, 40-2, 10-2)
    else
      c.fillRect(118, y, 40, 10)
    y -= 12
  }
  c.strokeStyle='rgb(210,255,255)'
  c.beginPath()
  c.moveTo(18,20)
  c.lineTo(90,20)
  c.moveTo(188,20)
  c.lineTo(268,20)
  c.stroke()
}

function timeRem(secs)
{
  h = (secs / 3600).toFixed()
  m = (secs % 3600).toFixed()
  m = (m / 60).toFixed()
  if(m<10 && h) m='0'+m
  s = (secs%60).toFixed()
  if(s<10) s='0'+s
  str = ''
  if(secs>=3600) str=h+':'
  str+=m+':'+s
  return str
}

mdays=[31,28,31,30,31,30,31,31,30,31,30,31]

function drawstuff(){
try {
  graph = $('#bars')
  var c=document.getElementById('bars')
  var c2=document.getElementById('bars2')

  tipCanvas=document.getElementById("tip")
  tipCtx=tipCanvas.getContext("2d")
  tipDiv=document.getElementById("popup")

  ctx=c.getContext("2d")
  ctx.fillStyle="#000"
  ctx.fillRect(0,0,c.width,c.height)
  ctx.fillStyle="#FFF"
  ctx.font="8px sans-serif"

  dots=[]
  date = new Date()
  ctx.lineWidth=9
  draw_scale(d.wattArr,c.width-5,c.height,date.getHours(),+d.wh,0,24)
  dotStart=dots.length
  // request mousemove events
  graph.mousemove(function(e){handleMouseMove(e);})
  graph2 = $('#bars2')
  graph2.mousemove(function(e){handleMouseMove2(e);})
  // show tooltip when mouse hovers over dot
  function handleMouseMove(e){
    rect=c.getBoundingClientRect()
    mouseX=e.clientX-rect.x
    mouseY=e.clientY-rect.y
    var hit = false
    for(i=0;i<dotStart;i++){
      dot = dots[i]
      if(mouseX>=dot.x && mouseX<=dot.x2 && mouseY>=dot.y && mouseY<=dot.y2){
        tipCtx.clearRect(0,0,tipCanvas.width,tipCanvas.height)
        tipCtx.fillStyle="#FFA"
        tipCtx.textAlign="right"
        tipCtx.lineWidth = 2
        tipCtx.fillStyle = "#000000"
        tipCtx.strokeStyle = '#333'
        tipCtx.font = 'bold 9pt sans-serif'
        if(+dot.tip>1000)
         txt=(dot.tip/1000).toFixed(1)+' KWh'
        else
         txt=dot.tip+' Wh'
        x=tipCanvas.width-3
        tipCtx.fillText(txt,x, 12)
        tipCtx.fillText('$'+(dot.tip*(0.15/1000)).toFixed(3),x,25)
        tipCtx.fillText(dot.tip2+String.fromCharCode(10514),x,38)
        tipCtx.fillText(dot.tip3+String.fromCharCode(10515),x,52)
        hit=true
        popup=document.getElementById("popup")
        popup.style.top=(dot.y+rect.y+window.pageYOffset+20)+"px"
        popup.style.left=(dot.x+rect.x)+"px"
        break
      }
    }
    if(!hit) popup.style.left="-200px"
  }

  c2=document.getElementById('bars2')
  ctx=c2.getContext("2d")
  ctx.fillStyle="#000"
  ctx.fillRect(0,0,c.width,c.height)
  ctx.fillStyle="#FFF"
  ctx.font="7px sans-serif"

  ctx.lineWidth=7
  if(date.getFullYear()%4==0) mdays[1]=29
  draw_scale(d.daily,c2.width-7,c2.height,date.getDate()-1,tot,1,mdays[date.getMonth()])

  function handleMouseMove2(e){
    rect=c2.getBoundingClientRect()
    mouseX=e.clientX-rect.x
    mouseY=e.clientY-rect.y
    var hit = false
    for(i=dotStart;i<dots.length;i++){
      dot = dots[i]
      if(mouseX>=dot.x && mouseX<=dot.x2 && mouseY>=dot.y && mouseY<=dot.y2){
        tipCtx.clearRect(0,0,tipCanvas.width,tipCanvas.height)
        tipCtx.fillStyle="#FFA"
        tipCtx.textAlign="right"
        tipCtx.lineWidth = 2
        tipCtx.fillStyle = "#000000"
        tipCtx.strokeStyle = '#333'
        tipCtx.font = 'bold 9pt sans-serif'
        if(+dot.tip>1000)
         txt=(dot.tip/1000).toFixed(1)+' KWh'
        else
         txt=dot.tip+' Wh'
        x=tipCanvas.width-3
        tipCtx.fillText(txt,x, 12)
        tipCtx.fillText('$'+(dot.tip*(0.15/1000)).toFixed(3),x,25)
        hit=true
        popup=document.getElementById("popup")
        popup.style.top=(dot.y+rect.y+window.pageYOffset+20)+"px"
        popup.style.left=(dot.x+rect.x)+"px"
        break
      }
    }
    if(!hit) popup.style.left="-200px"
  }

}catch(err){}
}

function draw_scale(arr,w,h,hi,cur,ofs,cnt)
{
  max=0
  min=2000
  tot=0
  for(i=0;i<cnt;i++)
  {
    if(arr[i]>max) max=arr[i]
    if(arr[i]&&arr[i]<min) min=arr[i]
    tot+=arr[i]
  }
  if(cur>max) max=cur
  if(min==2000) min=cur
  ctx.textAlign="center"
  for(i=0;i<cnt;i++)
  {
    x=i*(w/cnt)+8
    ctx.strokeStyle="#555"
    bh=arr[i]*(h-18)/max
    y=(3+h-18)-bh
    ctx.beginPath()
    ctx.moveTo(x,3+h-18)
    ctx.lineTo(x,y)
    ctx.stroke()
    if(i==hi){
      ctx.strokeStyle='rgb(0,120,255)'
      bh=cur*(h-18)/max
      y=(3+h-18)-bh
      ctx.beginPath()
      ctx.moveTo(x,3+h-18)
      ctx.lineTo(x,y)
      ctx.stroke()
    }
    ctx.strokeStyle="#FFF"
    ctx.fillText(i+ofs,x,3+h-7)
    if(arr[i])
      dots.push({
      x: x-(ctx.lineWidth/2),
      y: y,
      y2: y+bh,
      x2: x+ctx.lineWidth,
      tip: arr[i],
      tip2: wmax[i],
      tip3: wmin[i],
    })
  }
  ctx.textAlign="right"
  if(tot>1000)
   txt=(tot/1000).toFixed(1)+' KWh'
  else
   txt=tot.toFixed(1)+' Wh'
  ctx.fillText(txt,w+5,8)
  ctx.fillText('$'+(tot*ppkwh/1000).toFixed(3),w+4,18)
  ctx.fillText(max+String.fromCharCode(10514),w+4,33)
  ctx.fillText(min+String.fromCharCode(10515),w+4,43)
}
</script>
<style type="text/css">
#popup {
  position: absolute;
  top: 150px;
  left: -150px;
  z-index: 10;
  border-style: solid;
  border-width: 1px;
}
</style>
</head>
<body bgcolor="silver" onload="{
key=localStorage.getItem('key')
if(key!=null) document.getElementById('myKey').value=key
openSocket()
}">
<table width=278>
<tr><td><div id='topbar'></div></td></tr>

<tr><td>
  <input type="button" value="SHUTDOWN" onClick="{shutdown()}"> <input type="button" value="HIBERNATE" onClick="{hibernate()}"></td></tr> 
  <tr><td><input type="button" value="RESET DISP" onClick="{setVar('restart',0)}"><input type="button" value="POWER OFF" onClick="{confirmPwr()}"></td></tr>
</table>

<table width=278 >
<tr align="center"><td>
<div id="wrapper">
<canvas id="meter" width="270" height="105"></canvas>
<canvas id="bars" width="270" height="120"></canvas>
<div id="popup"><canvas id="tip" width=50 height=54></canvas></div>
<canvas id="bars2" width="270" height="120"></canvas>
</div>
</td></tr>
</table>
<table width=278>
<tr><td>Last cycle: </td><td id="cycle"></td></tr>
<tr><td>Health: </td><td id="health"></td></tr>
<tr><td>Cycles: </td><td id="cycles"></td></tr>
<tr><td>HID Warning % </td><td><input id="WCL" type=text size=4 onChange="{setVar('wcl', this.value)}"></td></tr>
<tr><td>HID Shutdown %</td><td><input id="RCL" type=text size=4 onChange="{setVar('rcl', this.value)}"></td></tr>
<tr><td>PPKWH <input id="ppkwh" type=text size=4 onChange="{setVar('ppkwh',(ppkwh=+this.value*100).toFixed() )}"> </td>
<td><input id="myKey" name="key" type=text size=40 placeholder="password" style="width: 100px" onChange="{localStorage.setItem('key', key = document.all.myKey.value)}"></td></tr>
</table>
</body>
</html>
)rawliteral";

const uint8_t favicon[] PROGMEM = {
  0x1F, 0x8B, 0x08, 0x08, 0x70, 0xC9, 0xE2, 0x59, 0x04, 0x00, 0x66, 0x61, 0x76, 0x69, 0x63, 0x6F, 
  0x6E, 0x2E, 0x69, 0x63, 0x6F, 0x00, 0xD5, 0x94, 0x31, 0x4B, 0xC3, 0x50, 0x14, 0x85, 0x4F, 0x6B, 
  0xC0, 0x52, 0x0A, 0x86, 0x22, 0x9D, 0xA4, 0x74, 0xC8, 0xE0, 0x28, 0x46, 0xC4, 0x41, 0xB0, 0x53, 
  0x7F, 0x87, 0x64, 0x72, 0x14, 0x71, 0xD7, 0xB5, 0x38, 0x38, 0xF9, 0x03, 0xFC, 0x05, 0x1D, 0xB3, 
  0x0A, 0x9D, 0x9D, 0xA4, 0x74, 0x15, 0x44, 0xC4, 0x4D, 0x07, 0x07, 0x89, 0xFA, 0x3C, 0x97, 0x9C, 
  0xE8, 0x1B, 0xDA, 0x92, 0x16, 0x3A, 0xF4, 0x86, 0x8F, 0x77, 0x73, 0xEF, 0x39, 0xEF, 0xBD, 0xBC, 
  0x90, 0x00, 0x15, 0x5E, 0x61, 0x68, 0x63, 0x07, 0x27, 0x01, 0xD0, 0x02, 0xB0, 0x4D, 0x58, 0x62, 
  0x25, 0xAF, 0x5B, 0x74, 0x03, 0xAC, 0x54, 0xC4, 0x71, 0xDC, 0x35, 0xB0, 0x40, 0xD0, 0xD7, 0x24, 
  0x99, 0x68, 0x62, 0xFE, 0xA8, 0xD2, 0x77, 0x6B, 0x58, 0x8E, 0x92, 0x41, 0xFD, 0x21, 0x79, 0x22, 
  0x89, 0x7C, 0x55, 0xCB, 0xC9, 0xB3, 0xF5, 0x4A, 0xF8, 0xF7, 0xC9, 0x27, 0x71, 0xE4, 0x55, 0x38, 
  0xD5, 0x0E, 0x66, 0xF8, 0x22, 0x72, 0x43, 0xDA, 0x64, 0x8F, 0xA4, 0xE4, 0x43, 0xA4, 0xAA, 0xB5, 
  0xA5, 0x89, 0x26, 0xF8, 0x13, 0x6F, 0xCD, 0x63, 0x96, 0x6A, 0x5E, 0xBB, 0x66, 0x35, 0x6F, 0x2F, 
  0x89, 0xE7, 0xAB, 0x93, 0x1E, 0xD3, 0x80, 0x63, 0x9F, 0x7C, 0x9B, 0x46, 0xEB, 0xDE, 0x1B, 0xCA, 
  0x9D, 0x7A, 0x7D, 0x69, 0x7B, 0xF2, 0x9E, 0xAB, 0x37, 0x20, 0x21, 0xD9, 0xB5, 0x33, 0x2F, 0xD6, 
  0x2A, 0xF6, 0xA4, 0xDA, 0x8E, 0x34, 0x03, 0xAB, 0xCB, 0xBB, 0x45, 0x46, 0xBA, 0x7F, 0x21, 0xA7, 
  0x64, 0x53, 0x7B, 0x6B, 0x18, 0xCA, 0x5B, 0xE4, 0xCC, 0x9B, 0xF7, 0xC1, 0xBC, 0x85, 0x4E, 0xE7, 
  0x92, 0x15, 0xFB, 0xD4, 0x9C, 0xA9, 0x18, 0x79, 0xCF, 0x95, 0x49, 0xDB, 0x98, 0xF2, 0x0E, 0xAE, 
  0xC8, 0xF8, 0x4F, 0xFF, 0x3F, 0xDF, 0x58, 0xBD, 0x08, 0x25, 0x42, 0x67, 0xD3, 0x11, 0x75, 0x2C, 
  0x29, 0x9C, 0xCB, 0xF9, 0xB9, 0x00, 0xBE, 0x8E, 0xF2, 0xF1, 0xFD, 0x1A, 0x78, 0xDB, 0x00, 0xEE, 
  0xD6, 0x80, 0xE1, 0x90, 0xFF, 0x90, 0x40, 0x1F, 0x04, 0xBF, 0xC4, 0xCB, 0x0A, 0xF0, 0xB8, 0x6E, 
  0xDA, 0xDC, 0xF7, 0x0B, 0xE9, 0xA4, 0xB1, 0xC3, 0x7E, 0x04, 0x00, 0x00, 
};
