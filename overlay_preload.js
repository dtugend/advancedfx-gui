const { contextBridge, ipcRenderer } = require('electron')

let domContentLoaded = false;
let input_captured = false;

document.addEventListener("afxEndInput", (event)=>{
  ipcRenderer.send("advancedfxAck-"+event.detail, input_captured)
}, {bubble: true});


ipcRenderer.on('afxEndInput', (event,id)=>{
  document.dispatchEvent(new CustomEvent('afxEndInput', {detail: id}))
})

document.addEventListener("keypress", (event)=>{
  input_captured = true;
}, {capture: true})
document.addEventListener("keydown", (event)=>{
  input_captured = true;
}, {capture: true})
document.addEventListener("keyup", (event)=>{
  input_captured = true;
}, {capture: true})
document.addEventListener("mousedown", (event)=>{
  input_captured = true;
})
document.addEventListener("mouseup", (event)=>{
  input_captured = true;
}, {capture: true})
document.addEventListener("mousewheel", (event)=>{
  input_captured = true;
}, {capture: true,passive: false})
document.addEventListener("mousemove", (event)=>{
  input_captured = true;
}, {capture: true})

// All of the Node.js APIs are available in the preload process.
// It has the same sandbox as a Chrome extension.
window.addEventListener('DOMContentLoaded', () => {


  document.addEventListener("keydown", (event)=>{
    if(!(event.target && (event.target.id=="capture" || event.target.tagName == "BODY" || event.target.tagName=="HTML"))) return;
    input_captured = false;
  })
  document.addEventListener("keypress", (event)=>{
    if(!(event.target && (event.target.id=="capture" || event.target.tagName == "BODY" || event.target.tagName=="HTML"))) return;
    input_captured = false;
  })  
  document.addEventListener("keyup", (event)=>{
    if(!(event.target && (event.target.id=="capture" || event.target.tagName == "BODY" || event.target.tagName=="HTML"))) return;
    input_captured = false;
  })
  document.addEventListener("mousedown", (event)=>{
    if(!(event.target && (event.target.id=="capture" || event.target.tagName == "BODY" || event.target.tagName=="HTML"))) return;
    input_captured = false;
  })
  document.addEventListener("mouseup", (event)=>{
    if(!(event.target && (event.target.id=="capture" || event.target.tagName == "BODY" || event.target.tagName=="HTML"))) return;
    input_captured = false;
  })
  document.addEventListener("mousewheel", (event)=>{
    if(!(event.target && (event.target.id=="capture" || event.target.tagName == "BODY" || event.target.tagName=="HTML"))) return;
    input_captured = false;
  }, {passive: false})
  document.addEventListener("mousemove", (event)=>{
    if(!(event.target && (event.target.id=="capture" || event.target.tagName == "BODY" || event.target.tagName=="HTML"))) return;
    input_captured = false;
  })

  const replaceText = (selector, text) => {
    const element = document.getElementById(selector)
    if (element) element.innerText = text
  }

  for (const type of ['chrome', 'node', 'electron']) {
    replaceText(`${type}-version`, process.versions[type])
  }

  domContentLoaded = true;
})
