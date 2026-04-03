/*
================================================================================
 [가이드: 게임 엔진의 뼈대 만들기]
================================================================================
 1. Component (기능): 캐릭터가 할 수 있는 '일' (이동, 시간 재기 등), Component들로 GameObject를 조립
 2. GameObject (객체): 게임에 존재하는 '물체' (플레이어, 타이머 등)
 3. GameWorld (세계): 모든 물체를 담고 있는 '바구니', 언리얼에서는 '레벨', 유니티에서는 '씬'이라고도 불림.

 * 구조: Component -> GameObject -> GameWorld 순으로 확장됨.
         (루프 한 번 돌 때 [입력 -> 업데이트 -> 렌더링] 순서로 모든 객체를 훑음.)
 [작동 원리]
 - Start(): 물체가 태어날 때 딱 한 번 실행되는 초기화 코드
 - Input(): 키보드/마우스 상태를 확인.
 - Update(): 수치(좌표 등)를 계산.
 - Render(): 화면에 결과를 출력.

================================================================================
*/
#include <d3d11.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <windows.h>
#include <vector>
#include <string>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <stdio.h>

/*
 * [강의 노트: DirectX 11 & Win32 GameLoop]
 * 1. WinMain: 프로그램의 입구
 * 2. WndProc: OS가 보낸 우편물(메시지)을 확인하는 곳
 * 3. GameLoop: 쉬지 않고 Update와 Render를 반복하는 엔진의 심장
 * 4. Release: 빌려온 GPU 메모리를 반드시 반납하는 습관 (메모리 누수 방지)
 */

#pragma comment(linker, "/entry:WinMainCRTStartup /subsystem:console")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib") // GPU를 사용하는 라이브러리

 // --- [전역 객체 관리] ---
 // DirectX 객체들은 GPU 메모리를 직접 사용함. 
 // 사용 후 'Release()'를 호출하지 않으면 프로그램 종료 후에도 메모리가 점유될 수 있음(메모리 누수).



struct Vertex 
{
    float x, y, z;
    float r, g, b, a;
};
typedef struct GameContext
{
    ID3D11Device* pd3dDevice = NULL;          // 리소스 생성자 (공장)  GPU랑 연결되어있는 통로, 
    ID3D11DeviceContext* pImmediateContext = NULL;   // 그리기 명령 수행 (일꾼) , 통로에 담아서 보낼 Command List
    IDXGISwapChain* pSwapChain = NULL;          // 화면 전환 (더블 버퍼링)  , 모니터의 메모리와 GPU의 메모리를 스왑핑
    ID3D11RenderTargetView* pRenderTargetView = NULL;   // 그림을 그릴 도화지(View) , 
    ID3D11PixelShader* pPixelShader = NULL;         // 픽셀 셰이더
    ID3D11VertexShader* pVertexShader = NULL;       // 정점 셰이더
    ID3D11InputLayout* pVertexLayout = NULL;               // Vertex 구조 설명서
    bool bFullScreen = false;
};

GameContext* g_gamecontext = NULL;

// HLSL (High-Level Shading Language) 소스
const char* shaderSource = R"(
struct VS_INPUT { float3 pos : POSITION; float4 col : COLOR; };
struct PS_INPUT { float4 pos : SV_POSITION; float4 col : COLOR; };

PS_INPUT VS(VS_INPUT input) {
    PS_INPUT output;
    output.pos = float4(input.pos, 1.0f); // 3D 좌표를 4D로 확장
    output.col = input.col;
    return output;
}

float4 PS(PS_INPUT input) : SV_Target {
    return input.col; // 정점에서 계산된 색상을 픽셀에 그대로 적용
}
)";
void ToggleWindow()
{
    g_gamecontext->pSwapChain->SetFullscreenState(g_gamecontext->bFullScreen, nullptr);
    g_gamecontext->bFullScreen = !g_gamecontext->bFullScreen;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message)    //윈도우의 이벤트는 메시지에 담김
    {
    default:
        // 우리가 관심 없는 메시지(창 크기 조절, 포커스 변경 등)는 OS가 기본값으로 처리함.
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
}
int InitDX11(HWND hWnd);

int MakeWindow(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // 1. 윈도우 등록 및 생성
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEX) };
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = L"DX11GameLoopClass";
    RegisterClassExW(&wcex);

    HWND hWnd = CreateWindowW(L"DX11GameLoopClass", L"과제: 움직이는 두 삼각형 만들기",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) return -1;
    ShowWindow(hWnd, nCmdShow);

    InitDX11(hWnd);
}

int InitDX11(HWND hWnd)
{
    // 2. DX11 디바이스 및 스왑 체인 초기화 , WinwdoManager 경우에는 실시간으로 그림 /대신 메모리에 미리 그려놓고 스왑
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 800; sd.BufferDesc.Height = 600;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;

    // GPU와 통신할 통로(Device)와 화면(SwapChain)을 생성함.
    // 디바이스 , 스왑체인, 컨텍스트 생성
    D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &sd, &g_gamecontext->pSwapChain, &g_gamecontext->pd3dDevice, nullptr, &g_gamecontext->pImmediateContext);

    // 렌더 타겟 설정 (도화지 준비)
    // 렌더 타겟 뷰 생성
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_gamecontext->pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    g_gamecontext->pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_gamecontext->pRenderTargetView);
    pBackBuffer->Release(); // 뷰를 생성했으므로 원본 텍스트는 바로 해제 (중요!)

    // 3. 셰이더 컴파일 및 생성
    // 버텍스 셰이더, 픽셀 셰이더 생성
    ID3DBlob* vsBlob, * psBlob;
    D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "VS", "vs_4_0", 0, 0, &vsBlob, nullptr);
    D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "PS", "ps_4_0", 0, 0, &psBlob, nullptr);
    g_gamecontext->pd3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_gamecontext->pVertexShader);
    g_gamecontext->pd3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_gamecontext->pPixelShader);

    // 정점의 데이터 형식을 정의 (IA 단계에 알려줌)
    // Input LayOut 생성
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    g_gamecontext->pd3dDevice->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_gamecontext->pVertexLayout);
    vsBlob->Release(); psBlob->Release(); 

    if (g_gamecontext->pd3dDevice == nullptr) return -1;
    if (g_gamecontext->pImmediateContext == nullptr) return -1;
    if (g_gamecontext->pSwapChain == nullptr) return -1;
    if (g_gamecontext->pRenderTargetView == nullptr) return -1;
    if (g_gamecontext->pPixelShader == nullptr) return -1;
    if (g_gamecontext->pVertexShader == nullptr) return -1;
    if (g_gamecontext->pVertexLayout == nullptr) return -1;
}

void ClearGameContext()
{
    if (g_gamecontext->pd3dDevice) g_gamecontext->pd3dDevice->Release();
    if (g_gamecontext->pImmediateContext) g_gamecontext->pImmediateContext->Release();
    if (g_gamecontext->pSwapChain) g_gamecontext->pSwapChain->Release();
    if (g_gamecontext->pRenderTargetView) g_gamecontext->pRenderTargetView->Release();
    if (g_gamecontext->pPixelShader) g_gamecontext->pPixelShader->Release();
    if (g_gamecontext->pVertexShader) g_gamecontext->pVertexShader->Release();
    if (g_gamecontext->pVertexLayout) g_gamecontext->pVertexLayout->Release();
}

// [1단계:컴포넌트 기저 클래스]
// 모든 기능(이동, 렌더링 등)은 이 클래스를 상속받아야 함.
class Component
{
public:
    class GameObject* pOwner = nullptr; // 이 기능이 누구의 것인지 저장
    bool isStarted = 0;           // Start()가 실행되었는지 체크

    virtual void Start() = 0;              // 초기화
    virtual void Input() {}                // 입력 (선택사항)
    virtual void Update(float dt) = 0;     // 로직 (필수)
    virtual void Render() {}               // 그리기 (선택사항)

    virtual ~Component() {}
};


// [2단계: 게임 오브젝트 클래스]
// 컴포넌트들을 담는 바구니 역할을 함.
class GameObject {
public:
    std::string name;
    std::vector<Component*> components;
    float x = 0.f, y = 0.f;
    GameObject(std::string n)
    {
        name = n;
    }
    GameObject(std::string n, float x, float y)
    {
        name = n;
        this->x = x;
        this->y = y;
    }


    // 객체가 죽을 때 담고 있던 컴포넌트들도 메모리에서 해제함
    ~GameObject() {
        for (int i = 0; i < (int)components.size(); i++)
        {
            delete components[i];
        }
    }

    // 새로운 기능을 추가하는 함수
    void AddComponent(Component* pComp)
    {
        pComp->pOwner = this;
        pComp->isStarted = false;
        components.push_back(pComp);
    }
};

// --- [3단계: 실제 구현할 기능 컴포넌트들] ---

// 기능 1: 플레이어 조종 및 이동
class PlayerControl : public Component {
public:
    float  speed;
    bool moveUp, moveDown, moveLeft, moveRight;
    bool bLeft = false;
    void Start() override
    {
        speed = 1.0f;
        moveUp = moveDown = moveLeft = moveRight = false;
        printf("[%s] PlayerControl 기능 시작!\n", pOwner->name.c_str());
    }

    // [입력 단계] 키 상태만 체크함
    void Input() override
    {
        if (bLeft)
        {
            moveUp = (GetAsyncKeyState('W') & 0x8000);
            moveDown = (GetAsyncKeyState('S') & 0x8000);
            moveLeft = (GetAsyncKeyState('A') & 0x8000);
            moveRight = (GetAsyncKeyState('D') & 0x8000);
        }
        else
        {
            moveUp = (GetAsyncKeyState(VK_UP) & 0x8000);
            moveDown = (GetAsyncKeyState(VK_DOWN) & 0x8000);
            moveLeft = (GetAsyncKeyState(VK_LEFT) & 0x8000);
            moveRight = (GetAsyncKeyState(VK_RIGHT) & 0x8000);
        }
    }

    // [업데이트 단계] 체크된 키 상태로 좌표만 계산함
    void Update(float dt) override
    {
        if (moveUp)    pOwner->y += speed * dt;
        if (moveDown)  pOwner->y -= speed * dt;
        if (moveLeft)  pOwner->x -= speed * dt;
        if (moveRight) pOwner->x += speed * dt;
    }
};

class GameLoop
{
public:
    bool isRunning;
    std::vector<GameObject*> gameWorld;
    std::chrono::high_resolution_clock::time_point prevTime;
    float deltaTime;   //delta time;

    //초기화
    void Initialize()
    {
        //초기화시 동작준비됨
        isRunning = true;

        gameWorld.clear();

        // 시간 측정 준비
        prevTime = std::chrono::high_resolution_clock::now();
        deltaTime = 0.0f;
    }

    void Input()
    {
        MSG msg = { 0 };
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) 
         {
             TranslateMessage(&msg);
            DispatchMessage(&msg);
         }
   
        // esc 누르면 종료
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) isRunning = false;
        if (GetAsyncKeyState('F') & 0x8000) ToggleWindow();

        // B. 입력 단계 (Input Phase)
        for (int i = 0; i < (int)gameWorld.size(); i++)
        {
            for (int j = 0; j < (int)gameWorld[i]->components.size(); j++)
            {
                gameWorld[i]->components[j]->Input();
            }
        }
    }

    void Update()
    {
        // C. 스타트 실행
        for (int i = 0; i < (int)gameWorld.size(); i++)
        {
            for (int j = 0; j < (int)gameWorld[i]->components.size(); j++)
            {
                // Start()가 호출된 적 없다면 여기서 호출 (유니티 방식)
                if (gameWorld[i]->components[j]->isStarted == false)
                {
                    gameWorld[i]->components[j]->Start();
                    gameWorld[i]->components[j]->isStarted = true;
                }
            }
        }

        // D. 업데이트 단계 (Update Phase)
        for (int i = 0; i < (int)gameWorld.size(); i++)
        {
            for (int j = 0; j < (int)gameWorld[i]->components.size(); j++)
            {
                gameWorld[i]->components[j]->Update(deltaTime);
            }
        }
    }

    void Render()
    {
        float clearColor[] = { 0.1f, 0.2f, 0.3f, 1.0f };
        g_gamecontext->pImmediateContext->ClearRenderTargetView(g_gamecontext->pRenderTargetView, clearColor);

        // 렌더링 파이프라인 상태 설정
        g_gamecontext->pImmediateContext->OMSetRenderTargets(1, &g_gamecontext->pRenderTargetView, nullptr);
        D3D11_VIEWPORT vp = { 0, 0, 800, 600, 0.0f, 1.0f };
        g_gamecontext->pImmediateContext->RSSetViewports(1, &vp);

        // E. 렌더링 단계 (Render Phase) -> 각 컴포넌트들의 Render를 함 
        for (int i = 0; i < (int)gameWorld.size(); i++)
        {
            for (int j = 0; j < (int)gameWorld[i]->components.size(); j++)
            {
                gameWorld[i]->components[j]->Render();
            }
        }

        // 화면 교체 (프론트 버퍼와 백 버퍼 스왑)
        g_gamecontext->pSwapChain->Present(1, 0);

    }
    void Run()
    {
        // --- [무한 게임 루프] ---
        while (isRunning) {

            // A. 시간 관리 (DeltaTime 계산)
            std::chrono::high_resolution_clock::time_point currentTime = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float> elapsed = currentTime - prevTime;
            deltaTime = elapsed.count();
            prevTime = currentTime;

            Input();
            Update();
            Render();

            // CPU 과부하 방지 (약 60~100 FPS 유지 시도)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    GameLoop()
    {
        Initialize();
    }
    ~GameLoop()
    {
        // [정리] 메모리 해제
        for (int i = 0; i < (int)gameWorld.size(); i++)
        {
            delete gameWorld[i]; // GameObject 소멸자가 컴포넌트들도 지움
        }
    }

};

class ComponentRenderer : public Component
{
private:
    ID3D11Buffer* pVertexBuffer = nullptr;
    float r = 0, g = 0, b = 0, a = 0;
public:
    ComponentRenderer() {};
    ComponentRenderer(float r, float g, float b, float a)
    {
        this->r = r;
        this->g = g;
        this->b = b;
        this->a = a;
    }
    void Start() override {};
    void Update(float dt) override
    {
        Vertex vertices[] = 
        {
        {0.0f + pOwner->x,  0.577f + pOwner->y, 0.0f,  r, g, b, a},
        {0.5f + pOwner->x, -0.289f + pOwner->y, 0.0f,  r, g, b, a},
        {-0.5f + pOwner->x, -0.289f + pOwner->y, 0.0f,  r, g, b, a}
        };

        if (pVertexBuffer) pVertexBuffer->Release();
        D3D11_BUFFER_DESC bd = { sizeof(vertices), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA initData = { vertices, 0, 0 };
        g_gamecontext->pd3dDevice->CreateBuffer(&bd, &initData, &pVertexBuffer);
    }
    // [렌더링 단계] 계산된 좌표를 화면에 그림
    void Render() override
    {
        // 실제 엔진이라면 여기서 DirectX Draw를 부름
        g_gamecontext->pImmediateContext->IASetInputLayout(g_gamecontext->pVertexLayout);
        UINT stride = sizeof(Vertex), offset = 0;
        g_gamecontext->pImmediateContext->IASetVertexBuffers(0, 1, &pVertexBuffer, &stride, &offset);

        // Primitive Topology 설정: 삼각형 리스트로 연결하라!
        g_gamecontext->pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_gamecontext->pImmediateContext->VSSetShader(g_gamecontext->pVertexShader, nullptr, 0);
        g_gamecontext->pImmediateContext->PSSetShader(g_gamecontext->pPixelShader, nullptr, 0);

        // 최종 그리기 , (정점 개수)
        g_gamecontext->pImmediateContext->Draw(3, 0);
    }
    ~ComponentRenderer()
    {
        if (pVertexBuffer) pVertexBuffer->Release();
    }
};



// WinMain은 Main함수 
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    GameContext gc = { 0 };
    g_gamecontext = &gc;

    // 윈도우 생성 및 DXD11 설정
    if (MakeWindow(hInstance, hPrevInstance, lpCmdLine, nCmdShow) == -1)
    {
        return 0;
    }
    
    //게임루프
    GameLoop gLoop;
    gLoop.Initialize();

    // 시스템 정보 객체 조립
    GameObject* sysInfo = new GameObject("SystemManager");
    gLoop.gameWorld.push_back(sysInfo);

    // 플레이어 객체 조립
    GameObject* leftPlayer = new GameObject("Player2", -0.2, 0);
    GameObject* rightPlayer = new GameObject("Player1", 0.2, 0);
    PlayerControl* lControl = new PlayerControl();
    PlayerControl* rControl = new PlayerControl();
    lControl->bLeft = true;
    ComponentRenderer* lCR = new ComponentRenderer();
    ComponentRenderer* rCR = new ComponentRenderer(1.0f, 1.0f, 1.0f, 1.0f);
    leftPlayer->AddComponent(lControl);
    leftPlayer->AddComponent(lCR);
    rightPlayer->AddComponent(rControl);
    rightPlayer->AddComponent(rCR);
    gLoop.gameWorld.push_back(leftPlayer);
    gLoop.gameWorld.push_back(rightPlayer);

    //게임루프 실행
    gLoop.Run();

    // --- [6. 자원 해제 (Release)] ---
    // 생성(Create)한 모든 객체는 프로그램 종료 전 반드시 Release 해야 함.
    // 생성의 역순으로 해제하는 것이 관례임.
    // 존나게 중요함 필수!!!! 잘못하면 칼침맞음!!!!!
    ClearGameContext();
};
