// Modules to control application life and create native browser window
const {app, ipcMain, BrowserWindow} = require('electron');
const { write } = require('fs');
const { type } = require('os');
const path = require('path')
const advancedfx_gui_native = require('bindings')('advancedfx_gui_native')
const jsonrpc = require('./modules/jsonrpc.js');

app.disableHardwareAcceleration()

let writePipe
let readPipe

function createWindow () {
  // Create the browser window.
  const mainWindow = new BrowserWindow({
    width: 800,
    height: 600,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js')
    }
  })

  let overlayWindow;
  let overlayTexture;

  // and load the index.html of the app.
  mainWindow.loadFile('index.html')

  // Open the DevTools.
  // mainWindow.webContents.openDevTools()

  serverWritePipe = new advancedfx_gui_native.AnonymousPipe();
  serverReadPipe = new advancedfx_gui_native.AnonymousPipe();
  clientWritePipe = new advancedfx_gui_native.AnonymousPipe();
  clientReadPipe = new advancedfx_gui_native.AnonymousPipe();

  mainWindow.on('closed', function(){
    
  })

  let serverWritePipeReadHandle = serverWritePipe.nativeReadHandleToLong();
  let serverReadPipeWriteHandle = serverReadPipe.nativeWriteHandleToLong();

  let jsonRpcServer = new jsonrpc.JsonRpc_2_0_Server(async () => { return await serverReadPipe.readString(); }, async(value) => { await serverWritePipe.writeString(value); });

  jsonRpcServer.on('GetAfxHookSourceServerReadHandle', async () => {
    return clientWritePipe.nativeReadHandle();
  });
  jsonRpcServer.on('GetAfxHookSourceServerWriteHandle', async () => {
    return clientReadPipe.nativeWriteHandle();
  });
  jsonRpcServer.on('DrawingWindowCreated', async (adapterLuid,width,height) => {
    overlayTexture = new advancedfx_gui_native.SharedTexture(adapterLuid,width,height);
    clientWritePipe.writeString(JSON.stringify({
      "jsonrpc": "2.0",
      "method": "SetSharedTextureHandle",
      "params": [overlayTexture.getSharedHandle()]
    }));
    overlayWindow = new BrowserWindow({
      show: false,
      frame: false,
     "width": width,
     "height": height,
      webPreferences: {
        offscreen: true,
        transparent: true
      }
    })
    clientWritePipe.writeString(JSON.stringify({
      "jsonrpc": "2.0",
      "method": "SetMouseCursor",
      "params": ["help"]
    }));    
    overlayWindow.webContents.on("paint", (event, dirty, image) => {
      if(overlayTexture) {

        /*clientWritePipe.writeString(JSON.stringify({
          "jsonrpc": "2.0",
          "method": "LockSharedTexture",
          "params": []
        }));*/

        overlayTexture.update(dirty,image.getBitmap());

        console.log(dirty);

        /*clientWritePipe.writeString(JSON.stringify({
          "jsonrpc": "2.0",
          "method": "UnlockSharedTexture",
          "params": []
        }));*/
      }
    })
    overlayWindow.on("cursor-changed", (event,type) => {
      clientWritePipe.writeString(JSON.stringify({
        "jsonrpc": "2.0",
        "method": "SetMouseCursor",
        "params": [type]
      }));
    })
    overlayWindow.loadURL('https://github.com');
    overlayWindow.webContents.setFrameRate(12);
  });
  jsonRpcServer.on('DrawingWindowDestroyed', async() =>{
    clientWritePipe.writeString(JSON.stringify({
      "jsonrpc": "2.0",
      "method": "SetSharedTextureHandle",
      "params": [advancedfx_gui_native.getInvalidHandleValue()]
    }));
    if(overlayWindow) {
      overlayWindow.destroy();
      overlayWindow = null;
    }
    if(overlayTexture) {
      overlayTexture.delete();
      overlayTexture = null;
    }
  });
  jsonRpcServer.on('SendMouseInputEvent', async(ev)=>{
    return false;
  });
  jsonRpcServer.on('SendMouseWheelInputEvent', async(ev)=>{
    return false;
  });
  jsonRpcServer.on('SendKeyboardInputEvent', async(ev)=>{
    return false;
  });

  let pump = jsonRpcServer.pump();

  const { execFile }= require('child_process');
  execFile('C:\\source\\advancedfx-dtugend-v3-dev\\build\\Release\\bin\\hlae.exe', [
    '-customLoader',
    '-noGui',
    '-autoStart',
    '-addEnv', 'SteamPath=C:\\Program Files (x86)\\Steam',
    '-addEnv', 'SteamClientLaunch=1',
    '-addEnv', 'SteamGameId=730',
    '-addEnv', 'SteamAppId=730',
    '-addEnv', 'SteamOverlayGameId=730',
    '-addEnv', 'USRLOCALCSGO=C:\\Users\\Dominik\\Desktop\\mmcfg',
    '-addEnv', `AFXGUI_PIPE_READ=${serverWritePipeReadHandle}`,
    '-addEnv', `AFXGUI_PIPE_WRITE=${serverReadPipeWriteHandle}`,
    '-hookDllPath', 'C:\\source\\advancedfx-dtugend-v3-dev\\build\\Release\\bin\\AfxHookSource.dll',
    '-programPath', 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\Counter-Strike Global Offensive\\csgo.exe',
    '-cmdLine', '-steam -insecure +sv_lan 1 -window -console -game csgo +echo "Hello World" -w 1280 -h 720'
    ], (error, stdout, stderr) => {
    if (error) {
      throw error;
    }
    console.log(stdout);
  });
}

// This method will be called when Electron has finished
// initialization and is ready to create browser windows.
// Some APIs can only be used after this event occurs.
app.whenReady().then(() => {
  createWindow()

  app.on('activate', function () {
    // On macOS it's common to re-create a window in the app when the
    // dock icon is clicked and there are no other windows open.
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

// Quit when all windows are closed, except on macOS. There, it's common
// for applications and their menu bar to stay active until the user quits
// explicitly with Cmd + Q.
app.on('window-all-closed', function () {
  async function asyncWindowAllClosed(){
    if(serverWritePipe) await serverWritePipe.close();
    if(serverReadPipe) await serverReadPipe.close();  
    if(clientWritePipe) await clientWritePipe.close();
    if(clientReadPipe) await clientReadPipe.close();
    await pump;
    if (process.platform !== 'darwin') app.quit()
  }
  asyncWindowAllClosed();
})

app.on('will-quit', function() {

})

// In this file you can include the rest of your app's specific main process
// code. You can also put them in separate files and require them here.

ipcMain.handle('jsonRequest', async (event,value) => {
  //await writePipe.writeString(value);
  //return await readPipe.readString();
})