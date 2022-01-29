#include <napi.h>

#include <queue>
#include <atomic>

#include <windows.h>

class CThreadedQueue {
  typedef std::function<void(void)> fp_t;

 public:
  CThreadedQueue();
  ~CThreadedQueue();

  void SignalQuit();
  bool HasQuit();
  void Join();

  void Queue(const fp_t& op);
  void Queue(fp_t&& op);

  void SetFinalizer(const fp_t& finalizer);
  void SetFinalizer(fp_t&& finalizer);

  CThreadedQueue(const CThreadedQueue& rhs) = delete;
  CThreadedQueue& operator=(const CThreadedQueue& rhs) = delete;
  CThreadedQueue(CThreadedQueue&& rhs) = delete;
  CThreadedQueue& operator=(CThreadedQueue&& rhs) = delete;

  std::thread::native_handle_type GetNativeThreadHandle(){
    return m_Thread.native_handle();
  }

 private:
  std::mutex m_Lock;
  std::thread m_Thread;
  std::queue<fp_t> m_Queue;
  std::condition_variable m_Cv;
  bool m_Quit = false;
  std::atomic_bool m_HasQuit = false;

  void Init(void);
  void QueueThreadHandler(void);
};


CThreadedQueue::CThreadedQueue() {
  m_Thread = std::thread(&CThreadedQueue::QueueThreadHandler, this);
}

CThreadedQueue::~CThreadedQueue() {
  Join();
}

void CThreadedQueue::SignalQuit() {
  m_Quit = true;
  m_Cv.notify_one();
}

bool CThreadedQueue::HasQuit() {
  return m_HasQuit;
}

void CThreadedQueue::Join() {
  if (m_Thread.joinable()) {
    m_Thread.join();
  }  
}

void CThreadedQueue::Queue(const fp_t& op) {

  std::unique_lock<std::mutex> lock(m_Lock);
  m_Queue.push(op);

  m_Cv.notify_one();
}

void CThreadedQueue::Queue(fp_t&& op) {

  std::unique_lock<std::mutex> lock(m_Lock);
  m_Queue.push(std::move(op));

  m_Cv.notify_one();
}

void CThreadedQueue::QueueThreadHandler(void) {
  std::unique_lock<std::mutex> lock(m_Lock);

  do {
    m_Cv.wait(lock, [this] { return (m_Queue.size() || m_Quit); });

    if (m_Queue.size()) {
      auto op = std::move(m_Queue.front());
      m_Queue.pop();

      lock.unlock();

      op();

      lock.lock();
    }
  } while (!m_Quit || m_Queue.size());

  m_HasQuit = true;
}

struct TsfnContext {
  Napi::ThreadSafeFunction tsfn;
  Napi::Value value;
};

class AnonymousPipe : public Napi::ObjectWrap<AnonymousPipe> {
 public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  AnonymousPipe(const Napi::CallbackInfo& info);
  virtual void Finalize(Napi::Env env) override;

 private:
  Napi::Value Close(const Napi::CallbackInfo& info);
  Napi::Value NativeReadHandle(const Napi::CallbackInfo& info);
  Napi::Value NativeWriteHandle(const Napi::CallbackInfo& info);
  Napi::Value NativeReadHandleToLong(const Napi::CallbackInfo& info);
  Napi::Value NativeWriteHandleToLong(const Napi::CallbackInfo& info);
  Napi::Value ReadArrayBuffer(const Napi::CallbackInfo& info);
  Napi::Value WriteArrayBuffer(const Napi::CallbackInfo& info);
  Napi::Value ReadString(const Napi::CallbackInfo& info);
  Napi::Value WriteString(const Napi::CallbackInfo& info);

  HANDLE m_ReadHandle = INVALID_HANDLE_VALUE;
  HANDLE m_WriteHandle = INVALID_HANDLE_VALUE;

  bool ReadBytes(void * pData, DWORD size);
  bool WriteBytes(const void * pData, DWORD size);

  CThreadedQueue * m_ThreadedQueue;
};

Napi::Object AnonymousPipe::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func =
    DefineClass(env, "AdvancedfxPipe", {
        InstanceMethod("close", &AnonymousPipe::Close),
        InstanceMethod("nativeReadHandle", &AnonymousPipe::NativeReadHandle),
        InstanceMethod("nativeWriteHandle", &AnonymousPipe::NativeWriteHandle),
        InstanceMethod("nativeReadHandleToLong", &AnonymousPipe::NativeReadHandleToLong),
        InstanceMethod("nativeWriteHandleToLong", &AnonymousPipe::NativeWriteHandleToLong),
        InstanceMethod("readArrayBuffer", &AnonymousPipe::ReadArrayBuffer),
        InstanceMethod("writeArrayBuffer", &AnonymousPipe::WriteArrayBuffer),
        InstanceMethod("readString", &AnonymousPipe::ReadString),
        InstanceMethod("writeString", &AnonymousPipe::WriteString),
    });

  Napi::FunctionReference* constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(func);
  env.SetInstanceData(constructor);

  exports.Set("AnonymousPipe", func);
  return exports;
}

AnonymousPipe::AnonymousPipe(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<AnonymousPipe>(info) {


  SECURITY_ATTRIBUTES securityAttributes {
    sizeof(SECURITY_ATTRIBUTES),
    NULL,
    TRUE
  };

  CreatePipe(&m_ReadHandle, &m_WriteHandle, &securityAttributes, 0);
  m_ThreadedQueue = new CThreadedQueue();
}

void AnonymousPipe::Finalize(Napi::Env env)
{
  if(m_ThreadedQueue) {
    m_ThreadedQueue->SignalQuit();
    while(!m_ThreadedQueue->HasQuit()) CancelSynchronousIo(m_ThreadedQueue->GetNativeThreadHandle());
    delete m_ThreadedQueue;
    m_ThreadedQueue = nullptr;
  }
  if(INVALID_HANDLE_VALUE != m_WriteHandle) { CloseHandle(m_WriteHandle); m_WriteHandle = INVALID_HANDLE_VALUE; }
  if(INVALID_HANDLE_VALUE != m_ReadHandle) { CloseHandle(m_ReadHandle); m_ReadHandle = INVALID_HANDLE_VALUE; }
}

Napi::Value AnonymousPipe::Close(const Napi::CallbackInfo& info) {

  if(!m_ThreadedQueue) {
    Napi::Error::New(info.Env(), "Pipe threaded queue already closed")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();    
  }

  Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

  auto tsfnContext = new TsfnContext();

  tsfnContext->tsfn = Napi::ThreadSafeFunction::New(
      info.Env(),
      Napi::Function::Function(), // JavaScript function called asynchronously
      "AnonymousPipe::Close", // Name
      0, // Unlimited queue
      1, // 1 thread initially
      tsfnContext,
      [](Napi::Env env, TsfnContext* context) {
        delete context;
      }
  );

  m_ThreadedQueue->Queue([this,tsfnContext,deferred]{
    tsfnContext->tsfn.BlockingCall([deferred](Napi::Env env, Napi::Function jsCallback) {
      deferred.Resolve(env.Undefined());
    });

    tsfnContext->tsfn.Release();
  });

  Finalize(info.Env());
  
  return deferred.Promise();
}

Napi::Value AnonymousPipe::NativeReadHandle(const Napi::CallbackInfo& info) {
  void* __ptr64 ptr = HandleToHandle64(this->m_ReadHandle);
  auto dict = Napi::Object::New(info.Env());
  dict["lo"] = Napi::Number::New(info.Env(),(int)((unsigned __int64)ptr & 0xFFFFFFFF));
  dict["hi"] = Napi::Number::New(info.Env(),(int)((unsigned __int64)ptr >> 32));
  return dict;
}

Napi::Value AnonymousPipe::NativeWriteHandle(const Napi::CallbackInfo& info) {
  void* __ptr64 ptr = HandleToHandle64(this->m_WriteHandle);
  auto dict = Napi::Object::New(info.Env());
  dict["lo"] = Napi::Number::New(info.Env(),(int)((unsigned __int64)ptr & 0xFFFFFFFF));
  dict["hi"] = Napi::Number::New(info.Env(),(int)((unsigned __int64)ptr >> 32));
  return dict;
}

Napi::Value AnonymousPipe::NativeReadHandleToLong(const Napi::CallbackInfo& info) {
  long value = HandleToLong(this->m_ReadHandle);
  return Napi::Number::New(info.Env(),value);
}

Napi::Value AnonymousPipe::NativeWriteHandleToLong(const Napi::CallbackInfo& info) {
  long value = HandleToLong(this->m_WriteHandle);
  return Napi::Number::New(info.Env(),value);
}

Napi::Value AnonymousPipe::ReadArrayBuffer(const Napi::CallbackInfo& info) {

  if(!m_ThreadedQueue) {
    Napi::Error::New(info.Env(), "Pipe threaded queue already closed")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();    
  }

  if (info.Length() != 1) {
    Napi::Error::New(info.Env(), "Expected exactly one argument")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }
  if (!info[0].IsArrayBuffer()) {
    Napi::Error::New(info.Env(), "Expected an ArrayBuffer")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }

  Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

  auto tsfnContext = new TsfnContext();

  tsfnContext->value = info[0];

  tsfnContext->tsfn = Napi::ThreadSafeFunction::New(
      info.Env(),
      Napi::Function::Function(), // JavaScript function called asynchronously
      "AnonymousPipe::ReadArrayBuffer", // Name
      0, // Unlimited queue
      1, // 1 thread initially
      tsfnContext,
      [](Napi::Env env, TsfnContext* context) {
        delete context;
      }
  );

  m_ThreadedQueue->Queue([this,tsfnContext,deferred]{

    auto buf = tsfnContext->value.As<Napi::ArrayBuffer>();

    if(!ReadBytes(reinterpret_cast<unsigned char *>(buf.Data()), buf.ByteLength())) {
      tsfnContext->tsfn.BlockingCall( [deferred]( Napi::Env env, Napi::Function jsCallback) {
        deferred.Reject(env.Undefined());
      });
    } else {
      tsfnContext->tsfn.BlockingCall( [deferred]( Napi::Env env, Napi::Function jsCallback) {
        deferred.Resolve(env.Undefined());
      });
    }

    tsfnContext->tsfn.Release();
  });
  
  return deferred.Promise();  
}

Napi::Value AnonymousPipe::WriteArrayBuffer(const Napi::CallbackInfo& info) {

  if(!m_ThreadedQueue) {
    Napi::Error::New(info.Env(), "Pipe threaded queue already closed")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();    
  }
  
  if (info.Length() != 1) {
    Napi::Error::New(info.Env(), "Expected exactly one argument")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }
  if (!info[0].IsArrayBuffer()) {
    Napi::Error::New(info.Env(), "Expected an ArrayBuffer")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }

  Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

  auto tsfnContext = new TsfnContext();

  tsfnContext->value = info[0];

  tsfnContext->tsfn = Napi::ThreadSafeFunction::New(
      info.Env(),
      Napi::Function::Function(), // JavaScript function called asynchronously
      "AnonymousPipe::WriteArrayBuffer", // Name
      0, // Unlimited queue
      1, // 1 thread initially
      tsfnContext,
      [](Napi::Env env, TsfnContext* context) {
        delete context;
      }
  );

  m_ThreadedQueue->Queue([this,tsfnContext,deferred]{

    auto buf = tsfnContext->value.As<Napi::ArrayBuffer>();

    if(!WriteBytes(reinterpret_cast<unsigned char *>(buf.Data()), buf.ByteLength())) {
      tsfnContext->tsfn.BlockingCall([deferred]( Napi::Env env, Napi::Function jsCallback) {
        deferred.Reject(env.Undefined());
      });
    } else {
      tsfnContext->tsfn.BlockingCall([deferred]( Napi::Env env, Napi::Function jsCallback) {
        deferred.Resolve(env.Undefined());
      });
    }

    tsfnContext->tsfn.Release();
  });
  
  return deferred.Promise();  
}

Napi::Value AnonymousPipe::ReadString(const Napi::CallbackInfo& info) {
  
  if(!m_ThreadedQueue) {
    Napi::Error::New(info.Env(), "Pipe threaded queue already closed")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();    
  }

  Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

  auto tsfnContext = new TsfnContext();

  tsfnContext->tsfn = Napi::ThreadSafeFunction::New(
      info.Env(),
      Napi::Function::Function(), // JavaScript function called asynchronously
      "AnonymousPipe::ReadString", // Name
      0, // Unlimited queue
      1, // 1 thread initially
      tsfnContext,
      [](Napi::Env env, TsfnContext* context) {
        delete context;
      }
  );

  m_ThreadedQueue->Queue([this,tsfnContext,deferred]{
    DWORD strLen = 0;
    if(!ReadBytes(&strLen, sizeof(strLen))) {
      tsfnContext->tsfn.BlockingCall([deferred]( Napi::Env env, Napi::Function jsCallback) {
        deferred.Reject(env.Undefined());
      });
      tsfnContext->tsfn.Release();
      return;
    }

    std::string inStr;
    std::vector<unsigned char> readBuffer(256);

    while(inStr.length() < strLen)
    {
      DWORD bytesRead = 0;
      DWORD chunkSize = min(readBuffer.size(), strLen - inStr.length());
      do {
        DWORD curBytesRead;
        if (!ReadFile(m_ReadHandle, (unsigned char*)(&readBuffer[0]) + bytesRead, chunkSize, &curBytesRead, NULL))  {
          tsfnContext->tsfn.BlockingCall([deferred]( Napi::Env env, Napi::Function jsCallback) {
            deferred.Reject(env.Undefined());
          });          
          tsfnContext->tsfn.Release();
          return;
        }
        bytesRead += curBytesRead;
      } while (bytesRead < chunkSize);
      inStr.append(readBuffer.begin(), readBuffer.begin() + bytesRead);
    }

    tsfnContext->tsfn.BlockingCall( [deferred,inStr]( Napi::Env env, Napi::Function jsCallback) {
      deferred.Resolve( Napi::String::New(env, inStr) );
    });
    tsfnContext->tsfn.Release();
  });
  
  return deferred.Promise();   
}


Napi::Value AnonymousPipe::WriteString(const Napi::CallbackInfo& info) {
    
  if(!m_ThreadedQueue) {
    Napi::Error::New(info.Env(), "Pipe threaded queue already closed")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();    
  }
  
  if (info.Length() != 1) {
    Napi::Error::New(info.Env(), "Expected exactly one argument")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }
  if (!info[0].IsString()) {
    Napi::Error::New(info.Env(), "Expected an String")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();
  }

  Napi::Promise::Deferred deferred = Napi::Promise::Deferred::New(info.Env());

  auto tsfnContext = new TsfnContext();

  tsfnContext->tsfn = Napi::ThreadSafeFunction::New(
      info.Env(),
      Napi::Function::Function(), // JavaScript function called asynchronously
      "AnonymousPipe::WriteString", // Name
      0, // Unlimited queue
      1, // 1 thread initially
      tsfnContext,
      [](Napi::Env env, TsfnContext* context) {
        delete context;
      }
  );

  m_ThreadedQueue->Queue([this,tsfnContext,deferred,stdString = std::string(info[0].As<Napi::String>())]{  

    DWORD strLen = stdString.size();
    if(
      !WriteBytes(&strLen, sizeof(strLen))
      || !WriteBytes(reinterpret_cast<const unsigned char *>(stdString.c_str()), stdString.size())
    ) {
      tsfnContext->tsfn.BlockingCall([deferred]( Napi::Env env, Napi::Function jsCallback) {
        deferred.Reject(env.Undefined());
      });            
    } else  {
      tsfnContext->tsfn.BlockingCall([deferred]( Napi::Env env, Napi::Function jsCallback) {
        deferred.Resolve(env.Undefined());
      });
    }
    tsfnContext->tsfn.Release();
  });
  
  return deferred.Promise();
}

bool AnonymousPipe::ReadBytes(void * pData, DWORD bytesToRead) {
  do {
    DWORD bytesRead = 0;
    if(!ReadFile(m_ReadHandle, pData, bytesToRead, &bytesRead, NULL)) {
      return false;      
    }
    bytesToRead -= bytesRead;
    pData = (unsigned char *)pData + bytesRead;
  } while(0 < bytesToRead);

  return true;
}

bool AnonymousPipe::WriteBytes(const void * pData, DWORD bytesToWrite) {
  do {
    DWORD bytesWritten = 0;
    if(!WriteFile(m_WriteHandle, pData, bytesToWrite, &bytesWritten, NULL)) {
      return false;      
    }
    bytesToWrite -= bytesWritten;
    pData = (const unsigned char *)pData + bytesWritten;
  } while(0 < bytesToWrite);

  return true;
}

////////////////////////////////////////////////////////////////////////////////

#include <d3d11.h>

class SharedTexture : public Napi::ObjectWrap<SharedTexture> {
 public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);  
  SharedTexture(const Napi::CallbackInfo& info);
  virtual void Finalize(Napi::Env env) override;  
 private:
  ID3D11Texture2D * m_StagingTexture = nullptr;
  ID3D11Texture2D * m_SharedTexture = nullptr;
  ID3D11DeviceContext* m_Ctx = nullptr;
  HANDLE m_SharedHandle = INVALID_HANDLE_VALUE;
  int m_Width = 0;
  int m_Height = 0;

  Napi::Value Delete(const Napi::CallbackInfo& info);
  Napi::Value SharedTexture::GetSharedHandle(const Napi::CallbackInfo& info);
  Napi::Value SharedTexture::Update(const Napi::CallbackInfo& info);

  void DoClose();
};

Napi::Object SharedTexture::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func =
    DefineClass(env, "SharedTexture", {
        InstanceMethod("delete", &SharedTexture::Delete),
        InstanceMethod("getSharedHandle", &SharedTexture::GetSharedHandle),
        InstanceMethod("update", &SharedTexture::Update),
    });

  Napi::FunctionReference* constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(func);
  env.SetInstanceData(constructor);

  exports.Set("SharedTexture", func);
  return exports;
}

SharedTexture::SharedTexture(const Napi::CallbackInfo& info)
: Napi::ObjectWrap<SharedTexture>(info) {

  if (info.Length() != 3) {
    Napi::Error::New(info.Env(), "Expected exactly 3 arguments")
        .ThrowAsJavaScriptException();
    return;
  }

  LUID targetLUID {};
  bool bOkay = info[0].IsObject();
  if(bOkay){
    Napi::Object obj = info[0].As<Napi::Object>();
    bOkay = obj.Has("lo") && obj.Has("hi");
    if(bOkay) {
      Napi::Value lo = obj.Get("lo");
      Napi::Value hi = obj.Get("hi");
      bOkay = lo.IsNumber() && hi.IsNumber();
      if(bOkay) {
        Napi::Number numLo = lo.As<Napi::Number>();
        Napi::Number numHi = hi.As<Napi::Number>();
        targetLUID.LowPart = (LONG)numLo.Int32Value();
        targetLUID.HighPart = (LONG)numHi.Int32Value();
      }
    }
  }

  if(!bOkay) {
    Napi::Error::New(info.Env(), "Expected adapter LUID Handle object as argument 0")
      .ThrowAsJavaScriptException();
    return;
  }

  if (!info[1].IsNumber() && info[2].IsNumber()) {
    Napi::Error::New(info.Env(), "Expected width and height Number for arguments 1 and 2")
        .ThrowAsJavaScriptException();
    return;
  }

  int32_t width = info[1].As<Napi::Number>().Int32Value();
  int32_t height = info[2].As<Napi::Number>().Int32Value();

  if(width < 1 || height < 1) {
    Napi::Error::New(info.Env(), "Arguments width and height must be at least 1")
        .ThrowAsJavaScriptException();
    return;
  }

  
  IDXGIFactory * pFactory;
  if(SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)(&pFactory) ))) {

    IDXGIAdapter * pActualAdapter = nullptr;
    {
      UINT i = 0; 
      IDXGIAdapter * pAdapter;
      while(pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND) 
      { 
        DXGI_ADAPTER_DESC desc;
        if(SUCCEEDED(pAdapter->GetDesc(&desc))) {
          if(desc.AdapterLuid.HighPart == targetLUID.HighPart && desc.AdapterLuid.LowPart == targetLUID.LowPart) {
            pAdapter->AddRef();
            pActualAdapter = pAdapter; 
          }
        }
        pAdapter->Release();
        ++i; 
      } 
    }

    if(pActualAdapter) {
      ID3D11Device * pDevice;
      ID3D11DeviceContext * pCtx;
      if(SUCCEEDED(D3D11CreateDevice(pActualAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &pDevice, NULL, &pCtx))) {

        D3D11_TEXTURE2D_DESC desc {
          (UINT)width,
          (UINT)height,
          1,
          1,
          DXGI_FORMAT_B8G8R8A8_UNORM,
          {1, 0},
          D3D11_USAGE_DYNAMIC,
          D3D11_BIND_SHADER_RESOURCE,
          D3D11_CPU_ACCESS_WRITE,
          0
        };

        if(SUCCEEDED(pDevice->CreateTexture2D(&desc,NULL,&m_StagingTexture))) {

          desc.Usage = D3D11_USAGE_DEFAULT;
          desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
          desc.CPUAccessFlags = 0;
          desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

          if(SUCCEEDED(pDevice->CreateTexture2D(&desc,NULL,&m_SharedTexture))) {

            m_SharedHandle = INVALID_HANDLE_VALUE;

            IDXGIResource* dxgiResource;

            if (SUCCEEDED(m_SharedTexture->QueryInterface(__uuidof(IDXGIResource),
                                                  (void**)&dxgiResource))) {
              if (FAILED(dxgiResource->GetSharedHandle(&m_SharedHandle))) {
                m_SharedHandle = INVALID_HANDLE_VALUE;
              }
              dxgiResource->Release();
            }

            if(INVALID_HANDLE_VALUE == m_SharedHandle) {
              Napi::Error::New(info.Env(), "Getting shared Handle failed")
                .ThrowAsJavaScriptException();
            }

            if(m_SharedTexture && INVALID_HANDLE_VALUE != m_SharedHandle) {
              pCtx->AddRef();
              m_Ctx = pCtx;
              m_Width = width;
              m_Height = height;
            } else {
              if(m_SharedTexture) {
                m_SharedTexture->Release();
                m_SharedTexture = nullptr;
              }
              if(m_StagingTexture) {
                m_StagingTexture->Release();
                m_StagingTexture = nullptr;
              }
            }
            
          } else {
            Napi::Error::New(info.Env(), "CreateTexture2D failed for shared texture")
            .ThrowAsJavaScriptException();
          }
        } else {
          Napi::Error::New(info.Env(), "CreateTexture2D failed for staging texture")
          .ThrowAsJavaScriptException();
        }

        pDevice->Release();
        pCtx->Release();
      } else {
        Napi::Error::New(info.Env(), "D3D11CreateDevice failed")
        .ThrowAsJavaScriptException();
      }

      pActualAdapter->Release();
    } else {
      Napi::Error::New(info.Env(), "Could not find adapater for given LUID")
        .ThrowAsJavaScriptException();
    }

    pFactory->Release();
  } else {
    Napi::Error::New(info.Env(), "Could not create IDXGIFactory")
        .ThrowAsJavaScriptException();
  }
}

void SharedTexture::Finalize(Napi::Env env) {
  DoClose();
}

Napi::Value SharedTexture::Delete(const Napi::CallbackInfo& info) {
  DoClose();
   return info.Env().Undefined();
}

void SharedTexture::DoClose() {
  if(m_StagingTexture) {
    m_StagingTexture->Release();
    m_StagingTexture = nullptr;
  }
  if(m_SharedTexture) {
    m_SharedTexture->Release();
    m_SharedTexture = nullptr;
  }
  if(m_Ctx) {
    m_Ctx->Release();
    m_Ctx = nullptr;
  }
  m_SharedHandle = INVALID_HANDLE_VALUE;
}

Napi::Value SharedTexture::GetSharedHandle(const Napi::CallbackInfo& info) {
  void* __ptr64 ptr = HandleToHandle64(this->m_SharedHandle);
  auto dict = Napi::Object::New(info.Env());
  dict["lo"] = Napi::Number::New(info.Env(),(int)((unsigned __int64)ptr & 0xFFFFFFFF));
  dict["hi"] = Napi::Number::New(info.Env(),(int)((unsigned __int64)ptr >> 32));
  return dict;
}

Napi::Value SharedTexture::Update(const Napi::CallbackInfo& info) {
  if(!(info.Length() == 2 && info[0].IsObject() && info[1].IsBuffer())) {
    Napi::Error::New(info.Env(), "Expected exactly 2 parameters: dirty Rectangle, Buffer")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();    
  }

  Napi::Object obj = info[0].As<Napi::Object>();
  if(!(obj.Has("x") && obj.Has("y") && obj.Has("width")&& obj.Has("height"))) {
    Napi::Error::New(info.Env(), "Parameter 0 not a rectangle")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();    
  }

  Napi::Value valX = obj["x"];
  Napi::Value valY = obj["y"];
  Napi::Value valWidth = obj["width"];
  Napi::Value valHeight = obj["height"];

  if(!(valX.IsNumber() && valY.IsNumber() && valWidth.IsNumber() && valHeight.IsNumber())) {
    Napi::Error::New(info.Env(), "Parameter 0 not a Rectangle")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();    
  }

  int x = valX.As<Napi::Number>().Int32Value();
  int y = valY.As<Napi::Number>().Int32Value();
  int width = valWidth.As<Napi::Number>().Int32Value();
  int height = valHeight.As<Napi::Number>().Int32Value();

  if(x < 0 || y < 0 || width > m_Width || height > m_Height || x + width > m_Width || y + height > m_Height) {
    Napi::Error::New(info.Env(), "Parameter 0 Rectangle is out of allowed bounds")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();    
  }

  auto buf = info[1].As<Napi::Buffer<char*>>();

  if(buf.ByteLength() != (size_t)4 * m_Width * m_Height) {
    Napi::Error::New(info.Env(), "Image Buffer size unexpected: "+std::to_string(buf.ByteLength())+" != "+std::to_string((size_t)4 * m_Width * m_Height))
        .ThrowAsJavaScriptException();
    return info.Env().Undefined(); 
  }

  D3D11_MAPPED_SUBRESOURCE mapped;

  if(SUCCEEDED(m_Ctx->Map(m_StagingTexture,0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) {
    unsigned char *pSrcData = reinterpret_cast<unsigned char *>(buf.Data());
    size_t srcRowSize = sizeof(unsigned char) * 4 * m_Width;
    if(mapped.pData != nullptr && pSrcData != nullptr) {
      for(size_t i = 0; i < (size_t)height; i++) {
        memcpy((unsigned char *)mapped.pData + ((size_t)y +i) * mapped.RowPitch  + (size_t)x * 4 * sizeof(unsigned char), pSrcData + ((size_t)y +i) * srcRowSize + (size_t)x * 4 * sizeof(unsigned char), sizeof(unsigned char) * 4 * width);
      }
    }

    m_Ctx->Unmap(m_StagingTexture,0);

    D3D11_BOX box = {
      (size_t)x,(size_t)y,0,
      (size_t)x+width,(size_t)y+height,1
    };    

    m_Ctx->CopySubresourceRegion(m_SharedTexture,0,x,y,0,m_StagingTexture,0,&box);
    m_Ctx->Flush();
  } else {
    Napi::Error::New(info.Env(), "ID3D11DeviceContext::Map failed")
        .ThrowAsJavaScriptException();
    return info.Env().Undefined();    
  }  

  return info.Env().Undefined();
}


////////////////////////////////////////////////////////////////////////////////

Napi::Value GetInvalidHandleValue(const Napi::CallbackInfo& info) {
  void* __ptr64 ptr = HandleToHandle64(INVALID_HANDLE_VALUE);
  auto dict = Napi::Object::New(info.Env());
  dict["lo"] = Napi::Number::New(info.Env(),(int)((unsigned __int64)ptr & 0xFFFFFFFF));
  dict["hi"] = Napi::Number::New(info.Env(),(int)((unsigned __int64)ptr >> 32));
  return dict;
}

////////////////////////////////////////////////////////////////////////////////

using namespace Napi;

Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  AnonymousPipe::Init(env, exports);

  SharedTexture::Init(env, exports);

  exports.Set(Napi::String::New(env, "getInvalidHandleValue"),
              Napi::Function::New(env, GetInvalidHandleValue));

  return exports;
}

NODE_API_MODULE(advancedfx_gui_native, InitAll)