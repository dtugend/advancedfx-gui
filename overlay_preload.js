const { contextBridge, ipcRenderer } = require('electron')

let input_not_captured;
let domContentLoaded = false;



// All of the Node.js APIs are available in the preload process.
// It has the same sandbox as a Chrome extension.
window.addEventListener('DOMContentLoaded', () => {

  const captureEl = document.getElementsByTagName('body')[0];

  document.addEventListener('mousedown', (event)=>{  
    if(event.x == -10000 && event.y == -10000) {
      let result = input_not_captured;
      input_not_captured = false;
      event.stopImmediatePropagation();
      event.preventDefault();      
      ipcRenderer.send('overlayInputCapturedResult', result === undefined || !result);
    }
  }, {capture: true})

  captureEl.addEventListener("keypress", (event)=>{
    if(event.eventPhase != Event.AT_TARGET) return;
    input_not_captured = true;
  })
  captureEl.addEventListener("keydown", (event)=>{
    if(event.eventPhase != Event.AT_TARGET) return;
    input_not_captured = true;
  })
  captureEl.addEventListener("keyup", (event)=>{
    if(event.eventPhase != Event.AT_TARGET) return;
    input_not_captured = true;
  })
  captureEl.addEventListener("mousedown", (event)=>{
    if(event.eventPhase != Event.AT_TARGET) return;
    input_not_captured = true;
  })
  captureEl.addEventListener("mouseup", (event)=>{
    if(event.eventPhase != Event.AT_TARGET) return;
    input_not_captured = true;
  })
  captureEl.addEventListener("wheel", (event)=>{
    if(event.eventPhase != Event.AT_TARGET) return;
    input_not_captured = true;
  })
  captureEl.addEventListener("mousemove", (event)=>{
    if(event.eventPhase != Event.AT_TARGET) return;
    input_not_captured = true;
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
