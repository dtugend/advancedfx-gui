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

  const captureEl = document.getElementById('capture');

  captureEl.addEventListener("keypress", (event)=>{
    if(event.eventPhase != Event.AT_TARGET) return;
    input_captured = false;
    event.stopImmediatePropagation();
    event.preventDefault();  
  })
  captureEl.addEventListener("keydown", (event)=>{
    if(event.eventPhase != Event.AT_TARGET) return;
    input_captured = false;
    event.stopImmediatePropagation();
    event.preventDefault();  
  })
  captureEl.addEventListener("keyup", (event)=>{
    if(event.eventPhase != Event.AT_TARGET) return;
    input_captured = false;
    event.stopImmediatePropagation();
    event.preventDefault();  
  })
  captureEl.addEventListener("mousedown", (event)=>{
    if(event.eventPhase != Event.AT_TARGET) return;
    input_captured = false;
    event.stopImmediatePropagation();
    event.preventDefault();  
  })
  captureEl.addEventListener("mouseup", (event)=>{
    if(event.eventPhase != Event.AT_TARGET) return;
    input_captured = false;
    event.stopImmediatePropagation();
    event.preventDefault();  
  })
  captureEl.addEventListener("mousewheel", (event)=>{
    if(event.eventPhase != Event.AT_TARGET) return;
    input_captured = false;
    event.stopImmediatePropagation();
    event.preventDefault();  
  }, {passive: false})
  captureEl.addEventListener("mousemove", (event)=>{
    if(event.eventPhase != Event.AT_TARGET) return;
    input_captured = false;
    event.stopImmediatePropagation();
    event.preventDefault();  
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
