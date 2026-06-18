//
// Game.cpp
//

#include "pch.h"
#include "Game.h"
#include "imgui.h"
#include "sound.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "WICTextureLoader.h"

extern void ExitGame() noexcept;
#include <vector>
#include <fstream>
#include <sstream>
#include <Audio.h>
#include <array>
#include <cstdint>
#include <filesystem>



std::vector<Note> notes;
std::vector<Beams> beams;

std::vector<Note> originalNotes;
static char filename[64] = "chartname";
NoteType currentNoteType = TAP;

int lane;
static float beamLenght = 2.0f;

using namespace DirectX;
using namespace DirectX::SimpleMath;

using Microsoft::WRL::ComPtr;
auto previoursTime = std::chrono::high_resolution_clock::now();

ImVec2 m_EditorScreenPos = { 0,0 };

// 「3D Path Preview」ミニ枠の簡易投影で使う係数。
// Render()のProject3DとUpdate()の逆投影(ScreenToWorldXZ)が必ず同じ値を使うよう、ここで一括管理する。
// (左パネル幅=200pxに収まるよう、元の係数より縮小済み)
constexpr float kPreviewScaleX = 0.1469f;   // sx = cx + x*kPreviewScaleX + z*kPreviewScaleXZ
constexpr float kPreviewScaleXZ = 0.0351f;
constexpr float kPreviewScaleY = 0.3512f;   // sy = cy - y*kPreviewScaleY - z*kPreviewScaleYZ
constexpr float kPreviewScaleYZ = 0.0543f;

static DirectX::SimpleMath::Vector3 LerpVector3(
    const DirectX::SimpleMath::Vector3& a,
    const DirectX::SimpleMath::Vector3& b,
    float t)
{
    return a + (b - a) * t;
}

static DirectX::SimpleMath::Vector3 EvaluateNotePosition(const Note& note, float now)
{
    float startTime = note.GetSpawnTime();
    float endTime = note.time;
    float duration = endTime - startTime;
    float t = 1.0f;

    if (duration > 0.0001f)
    {
        t = (now - startTime) / duration;
    }
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    const auto& p0 = note.spawnPos;
    const auto& p3 = note.judgePos;

    if (note.controlPoints.empty())
    {
        //始点と終点を決めて線を引くところ
        return LerpVector3(p0, p3, t);
    }

    if (note.controlPoints.size() == 1)
    {
        //ポイントを追加して2次ベジェにする
        const auto& p1 = note.controlPoints[0];
        auto a = LerpVector3(p0, p1, t);
        auto b = LerpVector3(p1, p3, t);
        return LerpVector3(a, b, t);
    }
    //ポイント3点なら3次ベジェに変更  ド・カステリョの法則を使用
    const auto& p1 = note.controlPoints[0];
    const auto& p2 = note.controlPoints[1];
    auto a = LerpVector3(p0, p1, t);
    auto b = LerpVector3(p1, p2, t);
    auto c = LerpVector3(p2, p3, t);
    auto d = LerpVector3(a, b, t);
    auto e = LerpVector3(b, c, t);
    return LerpVector3(d, e, t);
}


Game::Game() noexcept(false)
{

    m_deviceResources = std::make_unique<DX::DeviceResources>();
    // TODO: Provide parameters for swapchain format, depth/stencil format, and backbuffer count.
    //   Add DX::DeviceResources::c_AllowTearing to opt-in to variable rate displays.
    //   Add DX::DeviceResources::c_EnableHDR for HDR10 display.
    m_deviceResources->RegisterDeviceNotify(this);
}

Game::~Game()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

// Initialize the Direct3D resources required to run.
void Game::Initialize(HWND window, int width, int height)
{
    sound.Init();

    struct Load3DInfo {
        std::string filename;
        std::string texdirectoryname;
        Load3DInfo(std::string p1, std::string p2) {
            filename = p1;
            texdirectoryname = p2;
        }
    };

    std::array<Load3DInfo, 1> g_loadmodel =
    {
        Load3DInfo(
            "asset/Model/Dai/Daiza.obj",
            "asset/Model"),
    };

    m_deviceResources->SetWindow(window, width, height);

    m_deviceResources->CreateDeviceResources();
    CreateDeviceDependentResources();

    m_deviceResources->CreateWindowSizeDependentResources();
    CreateWindowSizeDependentResources();

    // TODO: Change the timer settings if you want something other than the default variable timestep mode.
    // e.g. for 60 FPS fixed timestep update logic, call:
    /*
    m_timer.SetFixedTimeStep(true);
    m_timer.SetTargetElapsedSeconds(1.0 / 60);
    */
    IMGUI_CHECKVERSION();

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui_ImplWin32_Init(window);
    ID3D11Device* device = m_deviceResources->GetD3DDevice();
    ID3D11DeviceContext* context = m_deviceResources->GetD3DDeviceContext();
    ImGui_ImplDX11_Init(device, context);
}

#pragma region Frame Update
// Executes the basic game loop.
void Game::Tick()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    //ImGui::ShowDemoWindow();
    m_timer.Tick([&]()
        {
            Update(m_timer);
        });

    Render();
}

// Updates the world.
void Game::Update(DX::StepTimer const& timer)
{
    if (GetAsyncKeyState(VK_LBUTTON) & 0x0001)
    {
        mousePressed = true;
    }
    else
    {
        mousePressed = false;
    }
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
    {
        mouseDown = true;
    }
    else
    {
        mouseDown = false;
    }



    float elapsedTime = float(timer.GetElapsedSeconds());
    if (GetAsyncKeyState('Q') & 0x0001)
    {
        oknort = true;
    }
    if (GetAsyncKeyState('W') & 0x0001 && oknort == true)
    {
        oknort = false;
    }
    if (GetAsyncKeyState('Z') & 0x0001)
    {
        mirrorMode = true;
    }
    if (GetAsyncKeyState('X') & 0x0001 && mirrorMode == true)
    {
        mirrorMode = false;
    }
    ImGui::SliderFloat("Long Interval", &LongInterval, 0.03f, 0.5f);
    if (LongStartPlaced)
    {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), ">>> START placed! Click END position <<<");
    }


    if (!isOutRange)
    {
        // メイン画面でのクリック処理。
        // ImGuiのスクリーン座標から、実際の3Dカメラ(m_view/m_proj)を
        // 使ったレイキャストでワールド座標を求める。
        ImVec2 mousePos = ImGui::GetMousePos();
        float mouseX = mousePos.x;
        float mouseY = mousePos.y;

        float screenW = ImGui::GetIO().DisplaySize.x;
        float screenH = ImGui::GetIO().DisplaySize.y;

        // ビューポートは画面全体(GetScreenViewport())に設定されているため、
        // NDC変換も画面全体(screenW x screenH)を基準に行う。
        //  view*projの逆行列を使って自前でアンプロジェクトする)
        float ndcX = (mouseX / screenW) * 2.0f - 1.0f;  //NDC-1～1に変換
        float ndcY = 1.0f - (mouseY / screenH) * 2.0f; // DirectXは上方向が+1ほかと逆　だから1.0f-が必要

        DirectX::SimpleMath::Matrix viewProj = m_view * m_proj; //ワールド座標を画面座標に変換行列
        DirectX::SimpleMath::Matrix viewProjInv = viewProj.Invert();    //画面クリック->ワールド座標変換
        //Invertによる逆処理

        // Vector3::Transform は内部でパースペクティブ除算(w除算)まで行ってくれるため、near/far それぞれのワールド座標が直接得られる。
        DirectX::SimpleMath::Vector3 nearPoint = DirectX::SimpleMath::Vector3::Transform(
            DirectX::SimpleMath::Vector3(ndcX, ndcY, 0.0f), viewProjInv);   //z=0 Near面　z=1 Far面
        DirectX::SimpleMath::Vector3 farPoint = DirectX::SimpleMath::Vector3::Transform(
            DirectX::SimpleMath::Vector3(ndcX, ndcY, 1.0f), viewProjInv);
        //クリック位置からNDC点を作る->Near面作って->Far面作る->ワールド座標に変換->レイを描く

        rayDir = farPoint - nearPoint;
        rayDir.Normalize();

        // ノーツの高さは常に y = -55.0f 固定。レイとこの平面の交点を
        // クリック位置のワールド座標として使う。
        constexpr float noteY = -55.0f;
        bool hasHit = false;    //レイがnoteYに当たったかどうか
        if (fabsf(rayDir.y) > 1e-5f)    //ほぼ0なら0にする 1e-5f==0.00001fとほぼ同じ
        {
            float t = (noteY - nearPoint.y) / rayDir.y; //どれだけ進んだらnoteYに到達するかを計算
            if (t > 0.0f)   //マイナス座標に行ってないかのチェック
            {
                worldPos = nearPoint + rayDir * t;  //worldPosの取得
                hasHit = true;
            }
        }

        if (mousePressed && hasHit)
        {
            float worldX = worldPos.x;
            float worldZ = worldPos.z;
            //yは今回固定中

            switch (m_EditorState)
            {
            case EditorState::IDLE:
            {
                selectedNoteIndex = -1;
                for (int i = 0; i < (int)notes.size(); i++)
                {
                    // ノーツのspawn位置をワールド空間上の距離でヒット判定
                    // (XZ平面上の距離。判定の許容半径は40ワールド単位)
                    const auto& sp = notes[i].spawnPos;
                    float dx = worldX - sp.x;
                    float dz = worldZ - sp.z;
                    if (dx * dx + dz * dz < 40.0f * 40.0f)
                    {
                        selectedNoteIndex = i;
                        m_EditorState = EditorState::EDITING;
                        break;
                    }
                }
            }
            break;

            case EditorState::PLACING_SPAWN:
            {
                m_TempNote.spawnPos = { worldX, noteY, worldZ };
                m_EditorState = EditorState::PLACING_JUDGE;
            }
            break;

            case EditorState::PLACING_JUDGE:
                m_TempNote.judgePos = { worldX, noteY, worldZ };
                //m_TempNote.time = clickTime;
                if (m_TempNote.time <= m_TempNote.spawnTime)
                {
                    m_TempNote.time = m_TempNote.spawnTime + 0.1f;
                }
                notes.push_back(m_TempNote);
                std::sort(notes.begin(), notes.end(),
                    [](const Note& a, const Note& b) {
                        return a.time < b.time;
                    });
                selectedNoteIndex = (int)notes.size() - 1;
                m_EditorState = EditorState::EDITING;
                break;

            default:
                break;
            }
        }


        if (GetAsyncKeyState(VK_RBUTTON) & 0x0001)
        {
            if (m_EditorState == EditorState::PLACING_JUDGE ||
                m_EditorState == EditorState::PLACING_SPAWN)
            {
                m_EditorState = EditorState::IDLE;
            }
            else
            {
                if (selectedNoteIndex >= 0 && selectedNoteIndex < (int)notes.size())
                {
                    notes.erase(notes.begin() + selectedNoteIndex);
                    selectedNoteIndex = -1;
                    m_EditorState = EditorState::IDLE;
                }
            }
        }

        if (GetAsyncKeyState(VK_ESCAPE) & 0x0001)
        {
            m_EditorState = EditorState::IDLE;
            selectedNoteIndex = -1;
        }
    }

    if (GetAsyncKeyState('J') & 0x8000)
    {
        cameraPos.x -= 1.0f;
    }
    if (GetAsyncKeyState('L') & 0x8000)
    {
        cameraPos.x += 1.0f;
    }
    if (GetAsyncKeyState('I') & 0x8000)
    {
        cameraPos.y += 1.0f;
    }
    if (GetAsyncKeyState('K') & 0x8000)
    {
        cameraPos.y -= 1.0f;
    }
    if (GetAsyncKeyState('O') & 0x8000)
    {
        cameraPos.z += 1.0f;
    }
    if (GetAsyncKeyState('U') & 0x8000)
    {
        cameraPos.z -= 1.0f;
    }


    JudgeNoteS();
    JudgeNoteD();
    JudgeNoteF();
    JudgeNoteJ();
    JudgeNoteK();
    JudgeNoteL();
    JudgeLongNoteS();
    JudgeLongNoteD();
    JudgeLongNoteF();
    JudgeLongNoteJ();
    JudgeLongNoteK();
    JudgeLongNoteL();


    if (GetAsyncKeyState(VK_LEFT) & 0x0001)
    {
        mirrorLane--;
    }
    if (GetAsyncKeyState(VK_RIGHT) & 0x0001)
    {
        mirrorLane++;
    }
    if (mirrorLane < 0) mirrorLane = 0;
    if (mirrorLane >= LANE_COUNT) mirrorLane = LANE_COUNT;






    scroll += ImGui::GetIO().MouseWheel * 40.0f;


    static bool KeySpacePrev = false;

    bool keySpace = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;

    if (keySpace && !KeySpacePrev) // ?????????
    {
        switch (bgmstate)
        {
        case BGMstate::stop:
            //???
            originalNotes = notes;
            musicTime = editorBeat * (60.0f / bpm);
            sound.SetPlayPositionSeconds(SOUND_LABEL_BGM001, musicTime);
            sound.Play(SOUND_LABEL_BGM001); // ?????G?f?B?^?????????
            bgmstate = BGMstate::play;
            break;

        case BGMstate::play:
            // ?m?[?c????????
            notes = originalNotes;
            success = 0;
            miss = 0;
            sound.Pause(SOUND_LABEL_BGM001); // ??~?i?r???????\?j
            bgmstate = BGMstate::pause;
            break;

        case BGMstate::pause:
            sound.Resume(SOUND_LABEL_BGM001); // ?r????????
            bgmstate = BGMstate::play;
            if (mirrorMode == true)
            {
                mirrorMode = !mirrorMode;
            }
            break;
        }
    }

    if (bgmstate == BGMstate::play)
    {
        musicTime += elapsedTime;
    }
    else
    {
        float wheel = ImGui::GetIO().MouseWheel;
        editorBeat += wheel;
        if (GetAsyncKeyState(VK_UP) & 0x8000)
        {
            editorBeat += 0.1f;
        }
        if (GetAsyncKeyState(VK_DOWN) & 0x8000)
        {
            editorBeat -= 0.1f;
        }
    }
    // ?O?t???[???????X?V
    KeySpacePrev = keySpace;


    if (bgmstate == BGMstate::play)
    {
        for (int i = 0; i < notes.size(); i++)
        {
            float diff = musicTime - notes[i].time;

            if (diff > 0.2f)
            {
                printf("MISS\n");
                notes.erase(notes.begin() + i);
                miss++;
                i--;
            }
        }
    }

    // TODO: Add your game logic here.
    elapsedTime;
}
#pragma endregion

#pragma region Frame Render
// Draws the scene.
void Game::Render()
{
    if (m_timer.GetFrameCount() == 0)
    {
        return;
    }

    Clear();


    m_deviceResources->PIXBeginEvent(L"Render");
    auto context = m_deviceResources->GetD3DDeviceContext();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    float screenW = ImGui::GetIO().DisplaySize.x;
    float screenH = ImGui::GetIO().DisplaySize.y;
    float leftW = 200.0f;
    float bottomH = 60.0f;
    float chartW = screenW - leftW; //???p?l????O??????iX?j
    float chartH = screenH - bottomH;

    float aspect = float(screenW) / float(screenH);
    //// ??H?????????u???X
    //m_view = Matrix::CreateLookAt(
    //    Vector3(cameraPos.x, cameraPos.y, cameraPos.z),
    //    Vector3(0.0f, -55.0f, 400.0f),  // XZ???????S???????????
    //    Vector3::UnitY
    //);
    // Render()??m_view?????X
    m_view = Matrix::CreateLookAt(
        Vector3(cameraPos.x, cameraPos.y, cameraPos.z),
        Vector3(cameraPos.x, -55.0f, cameraPos.z + 1.0f), // ?^????????
        Vector3::UnitZ  // Z????????
    );
    aspect = chartW / chartH;

    m_proj = Matrix::CreatePerspectiveFieldOfView(
        XMConvertToRadians(60.0f),  // 30??60??g??
        aspect,
        1.0f,
        5000.0f
    );

    // クリックでノーツを置けるのは、左パネル/ボトムバーを除いた
    // 青いメイン画面（本番プレイ画面）の中だけ。
    {
        ImVec2 mp = ImGui::GetMousePos();
        isOutRange = !(mp.x >= leftW && mp.x <= leftW + chartW &&
            mp.y >= 0.0f && mp.y <= chartH);
    }

    //左パネル======================================================
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(leftW, screenH - bottomH));
        //動かせない、大きさを変えれないように設定
        ImGui::Begin("LeftPanel", nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

        //デバッグログ
        ImGui::Text("worldPos: %.1f, %.1f, %.1f", worldPos.x, worldPos.y, worldPos.z);
        ImGui::Text("rayDir: %.3f, %.3f, %.3f", rayDir.x, rayDir.y, rayDir.z);
        const char* noteNames[] = { "TAP","LONG","antiMirror","MirrorNote",
                                     "SlideR","SlideL","SlideU","SlideD" };
        NoteType noteTypes[] = { TAP, LONG_START, UNTIMIRROR, MIRRORNOTE,
                                 SLIDE_RIGHT, SLIDE_LEFT, SLIDE_UP, SLIDE_DOWN };

        for (int i = 0; i < 8; i++)
        {
            bool selected = (currentNoteType == noteTypes[i]);
            if (selected)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
            if (ImGui::Button(noteNames[i], ImVec2(-1, 0)))
                currentNoteType = noteTypes[i];
            if (selected)
                ImGui::PopStyleColor();
        }

        ImGui::SeparatorText("Key");
        const char* keyNames[] = { "S","D","F","J","K","L" };
        for (int i = 0; i < 6; i++)
        {
            if (i > 0) ImGui::SameLine();
            bool sel = (m_TempNote.lane == i);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
            char label[4];
            sprintf_s(label, "%s", keyNames[i]);
            if (ImGui::Button(label, ImVec2(28, 0)))
                m_TempNote.lane = i;
            if (sel) ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Place");

        bool isPlacing = (m_EditorState == EditorState::PLACING_SPAWN ||
            m_EditorState == EditorState::PLACING_JUDGE);

        if (isPlacing)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));

        if (ImGui::Button(
            m_EditorState == EditorState::PLACING_SPAWN ? "Click: Spawn Pos" :
            m_EditorState == EditorState::PLACING_JUDGE ? "Click: Judge Pos" :
            "Place Note", ImVec2(-1, 0)))
        {
            if (!isPlacing)
            {
                m_TempNote = Note();
                m_TempNote.note = currentNoteType;
                m_EditorState = EditorState::PLACING_SPAWN;
            }
            else
            {
                m_EditorState = EditorState::IDLE;
            }
        }
        if (isPlacing)
            ImGui::PopStyleColor();

        switch (m_EditorState)
        {
        case EditorState::PLACING_SPAWN:
            ImGui::TextColored(ImVec4(0, 1, 0.5f, 1), "1. Click spawn position");
            break;
        case EditorState::PLACING_JUDGE:
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "2. Click judge position");
            break;
        case EditorState::EDITING:
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Editing selected note");
            break;
        default:
            ImGui::TextDisabled("Click note to select");
            break;
        }
        //セーブとか=============================================
        ImGui::SeparatorText("Fire");
        ImGui::InputText("##filename", filename, 64);
        if (ImGui::Button("Save", ImVec2(90, 0)))
        {
            if (mirrorMode) mirrorMode = false;
            SaveChart(filename);
        }

        ImGui::SameLine();
        if (ImGui::Button("Load", ImVec2(90, 0)))
        {
            LoadChart(filename);
        }
        ImGui::SeparatorText("Selected Note");
        if (selectedNoteIndex >= 0 && selectedNoteIndex < (int)notes.size())
        {
            Note& n = notes[selectedNoteIndex];

            ImGui::Text("Key:");
            ImGui::SameLine();
            const char* keyNames[] = { "S","D","F","J","K","L" };
            for (int i = 0; i < 6; i++)
            {
                if (i > 0) ImGui::SameLine();
                bool sel = (n.lane == i);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button,
                    ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                char label[8];
                sprintf_s(label, "%s##k%d", keyNames[i], i);
                if (ImGui::Button(label, ImVec2(24, 0))) n.lane = i;
                if (sel) ImGui::PopStyleColor();
            }

            ImGui::InputFloat("SpawnTime", &n.spawnTime, 0.01f);
            ImGui::InputFloat("JudgeTime", &n.time, 0.01f);
            float travel = n.time - n.GetSpawnTime();
            ImGui::Text("TravelTime: %.2f sec", travel);

            ImGui::Spacing();

            // SpawnPos
            ImGui::SeparatorText("Spawn Pos");
            float sp[3] = { n.spawnPos.x, n.spawnPos.y, n.spawnPos.z };
            if (ImGui::InputFloat3("##sp", sp))
                n.spawnPos = { sp[0], sp[1], sp[2] };

            // JudgePos
            ImGui::SeparatorText("Judge Pos");
            float jp[3] = { n.judgePos.x, n.judgePos.y, n.judgePos.z };
            if (ImGui::InputFloat3("##jp", jp))
                n.judgePos = { jp[0], jp[1], jp[2] };

            ImGui::SeparatorText("Control Points");
            ImGui::TextDisabled("0=line  1=quad  2=cubic");
            for (int i = 0; i < (int)n.controlPoints.size(); i++)
            {
                char label[32];
                sprintf_s(label, "P%d", i);
                float v[3] = {
                    n.controlPoints[i].x,
                    n.controlPoints[i].y,
                    n.controlPoints[i].z
                };
                if (ImGui::InputFloat3(label, v))
                    n.controlPoints[i] = { v[0], v[1], v[2] };
                ImGui::SameLine();
                char del[16];
                sprintf_s(del, "x##%d", i);
                if (ImGui::Button(del))
                {
                    n.controlPoints.erase(n.controlPoints.begin() + i);
                    break;
                }
            }
            if (ImGui::Button("+ Add Point", ImVec2(-1, 0)))
            {
                DirectX::SimpleMath::Vector3 mid;
                mid.x = (n.spawnPos.x + n.judgePos.x) * 0.5f;
                mid.y = (n.spawnPos.y + n.judgePos.y) * 0.5f;
                mid.z = (n.spawnPos.z + n.judgePos.z) * 0.5f;
                n.controlPoints.push_back(mid);
            }

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("Delete Note", ImVec2(-1, 0)))
            {
                notes.erase(notes.begin() + selectedNoteIndex);
                selectedNoteIndex = -1;
                m_EditorState = EditorState::IDLE;
            }
            ImGui::PopStyleColor();
        }


        // ---- 3D Path Preview (LeftPanel内に統合) ----
        {
            ImVec2 mousePos = ImGui::GetMousePos();

            float mouseX = mousePos.x;
            float mouseY = mousePos.y;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            m_EditorScreenPos = pos;
            m_ChartW = chartW;
            m_ChartH = chartH;
            chartPosX = pos.x;

            // isOutRange は「3D Path Preview」枠が確定した後（下記）で算出する

            // BPM
            bpm = 120;
            float pixelsPerBeat = 100.0f;
            float secondPerBeat = 60.0f / bpm;
            if (bgmstate == BGMstate::play)
                currentBeat = musicTime / secondPerBeat;
            else
                currentBeat = editorBeat;

            float worldXMin = -400.0f;
            float worldXMax = 400.0f;
            float worldXRange = worldXMax - worldXMin;


            float previewW = leftW - 20.0f;
            float previewH = 200.0f;
            ImVec2 previewMin(pos.x, pos.y);
            ImVec2 previewMax(previewMin.x + previewW, previewMin.y + previewH);
            ImVec2 previewCenter((previewMin.x + previewMax.x) * 0.5f, previewMin.y + previewH * 0.62f);

            // m_PreviewMin/Max/Center は「3D Path Preview」ミニ枠の表示用。
            // クリックでのノーツ配置判定は青いメイン画面(isOutRange)で行うため、
            // ここでは表示位置の保存のみ行う。
            m_PreviewMin = previewMin;
            m_PreviewMax = previewMax;
            m_PreviewCenter = previewCenter;
            m_PreviewValid = true;

            draw->AddRectFilled(previewMin, previewMax, IM_COL32(18, 20, 26, 220));
            draw->AddRect(previewMin, previewMax, IM_COL32(130, 150, 180, 180));
            draw->AddText(ImVec2(previewMin.x + 8.0f, previewMin.y + 6.0f),
                IM_COL32(220, 230, 255, 230), "3D Path Preview");

            // パネル内でプレビュー枠の分だけスペースを確保し、
            // 後続のウィジェットがプレビューと重ならないようにする
            ImGui::Dummy(ImVec2(previewW, previewH + 8.0f));

            auto Project3D = [&](const DirectX::SimpleMath::Vector3& v) -> ImVec2
                {
                    float sx = previewCenter.x + v.x * kPreviewScaleX + v.z * kPreviewScaleXZ;
                    float sy = previewCenter.y - v.y * kPreviewScaleY - v.z * kPreviewScaleYZ;
                    return ImVec2(sx, sy);
                };

            {
                //置いた後のミニモニター用
                ImVec2 origin = Project3D({ 0.0f, -55.0f, -210.0f });
                draw->AddLine(origin, Project3D({ 360.0f, -55.0f, -210.0f }), IM_COL32(255, 90, 90, 180), 1.5f);
                draw->AddLine(origin, Project3D({ 0.0f, -55.0f, 900.0f }), IM_COL32(120, 170, 255, 180), 1.5f);
                draw->AddLine(origin, Project3D({ 0.0f, 120.0f, -210.0f }), IM_COL32(120, 255, 150, 180), 1.5f);
                draw->AddText(Project3D({ 380.0f, -55.0f, -210.0f }), IM_COL32(255, 130, 130, 220), "X");
                draw->AddText(Project3D({ 0.0f, -55.0f, 940.0f }), IM_COL32(150, 190, 255, 220), "Z");
                draw->AddText(Project3D({ 0.0f, 135.0f, -210.0f }), IM_COL32(150, 255, 180, 220), "Y");
            }
            float previewTime = bgmstate == BGMstate::play ? musicTime : editorBeat * secondPerBeat;
            for (int i = 0; i < (int)notes.size(); i++)
            {
                const auto& note = notes[i];
                bool isSelected = (i == selectedNoteIndex);
                ImU32 pathCol = isSelected ? IM_COL32(255, 230, 80, 230) : IM_COL32(90, 190, 255, 150);
                float startT = note.GetSpawnTime();
                float endT = note.time;
                float rangeT = endT - startT;
                ImVec2 prev = Project3D(note.spawnPos);
                for (int s = 1; s <= 24; s++)
                {
                    float sampleT = startT + rangeT * (float)s / 24.0f;
                    ImVec2 cur = Project3D(EvaluateNotePosition(note, sampleT));
                    draw->AddLine(prev, cur, pathCol, isSelected ? 2.0f : 1.0f);
                    prev = cur;
                }

                ImVec2 spawnP = Project3D(note.spawnPos);
                ImVec2 judgeP = Project3D(note.judgePos);
                draw->AddCircleFilled(spawnP, 3.5f, IM_COL32(0, 210, 255, 210));
                draw->AddRectFilled(ImVec2(judgeP.x - 4.0f, judgeP.y - 4.0f),
                    ImVec2(judgeP.x + 4.0f, judgeP.y + 4.0f), IM_COL32(255, 110, 110, 230));

                if (previewTime >= startT && previewTime <= endT)
                {
                    ImVec2 bullet = Project3D(EvaluateNotePosition(note, previewTime));
                    draw->AddCircleFilled(bullet, isSelected ? 7.0f : 5.0f,
                        isSelected ? IM_COL32(255, 255, 80, 255) : IM_COL32(120, 255, 170, 230));
                }
            }

        }
        ImGui::End();
    }

    context->OMSetBlendState(m_states->Opaque(), nullptr, 0xFFFFFFFF);
    context->OMSetDepthStencilState(m_states->DepthDefault(), 0);
    context->RSSetState(m_states->CullNone());

    m_basicEffect->SetView(m_view);
    m_basicEffect->SetProjection(m_proj);
    m_basicEffect->Apply(context);
    context->IASetInputLayout(m_inputLayout.Get());

    m_primitiveBatch->Begin();

    m_primitiveBatch->DrawLine(
        VertexPositionColor(Vector3(-300.0f, -55.0f, -210.0f), Colors::Red),
        VertexPositionColor(Vector3(300.0f, -55.0f, -210.0f), Colors::Red)
    );

    m_primitiveBatch->DrawLine(
        VertexPositionColor(Vector3(-400.0f, -55.0f, 0.0f), Colors::White),
        VertexPositionColor(Vector3(400.0f, -55.0f, 0.0f), Colors::White)
    );

    for (float z = -200.0f; z <= 1000.0f; z += 100.0f)
    {
        XMVECTORF32 col = (fabs(z) < 0.1f) ? Colors::White : Colors::Gray;
        m_primitiveBatch->DrawLine(
            VertexPositionColor(Vector3(-400.0f, -55.0f, z), col),
            VertexPositionColor(Vector3(400.0f, -55.0f, z), col)
        );
    }

    for (int i = 0; i < (int)notes.size(); i++)
    {
        auto& note = notes[i];
        bool isSel = (i == selectedNoteIndex);

        XMVECTORF32 pathCol = isSel
            ? Colors::Yellow : Colors::Cyan;
        XMVECTORF32 spawnCol = isSel
            ? Colors::Yellow : Colors::LimeGreen;
        XMVECTORF32 judgeCol = isSel
            ? Colors::Orange : Colors::OrangeRed;

        float st = note.GetSpawnTime();
        float et = note.time;
        Vector3 prev = note.spawnPos;
        for (int s = 1; s <= 24; s++)
        {
            float sampleT = st + (et - st) * s / 24.0f;
            Vector3 cur = EvaluateNotePosition(note, sampleT);
            m_primitiveBatch->DrawLine(
                VertexPositionColor(prev, pathCol),
                VertexPositionColor(cur, pathCol)
            );
            prev = cur;
        }

        //始点と終点
        {
            float ms = 15.0f;
            m_primitiveBatch->DrawLine(
                VertexPositionColor(note.spawnPos + Vector3(-ms, 0, 0), spawnCol),
                VertexPositionColor(note.spawnPos + Vector3(ms, 0, 0), spawnCol)
            );
            m_primitiveBatch->DrawLine(
                VertexPositionColor(note.spawnPos + Vector3(0, 0, -ms), spawnCol),
                VertexPositionColor(note.spawnPos + Vector3(0, 0, ms), spawnCol)
            );

            float mj = 20.0f;
            m_primitiveBatch->DrawLine(
                VertexPositionColor(note.judgePos + Vector3(-mj, 0, 0), judgeCol),
                VertexPositionColor(note.judgePos + Vector3(mj, 0, 0), judgeCol)
            );
            m_primitiveBatch->DrawLine(
                VertexPositionColor(note.judgePos + Vector3(0, 0, -mj), judgeCol),
                VertexPositionColor(note.judgePos + Vector3(0, 0, mj), judgeCol)
            );
        }

        if (bgmstate == BGMstate::play &&
            musicTime >= st && musicTime <= et)
        {
            Vector3 nowPos = EvaluateNotePosition(note, musicTime);
            float ns = 12.0f;
            m_primitiveBatch->DrawLine(
                VertexPositionColor(nowPos + Vector3(-ns, 0, 0), Colors::Aqua),
                VertexPositionColor(nowPos + Vector3(ns, 0, 0), Colors::Aqua)
            );
            m_primitiveBatch->DrawLine(
                VertexPositionColor(nowPos + Vector3(0, -ns, 0), Colors::Aqua),
                VertexPositionColor(nowPos + Vector3(0, ns, 0), Colors::Aqua)
            );
        }
    }



    m_primitiveBatch->End();

    //下パネル=========================================================
    {
        ImGui::SetNextWindowPos(ImVec2(0, screenH - bottomH));
        ImGui::SetNextWindowSize(ImVec2(screenW, bottomH));
        ImGui::Begin("BottomBar", nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar
        );

        if (ImGui::Button("Play/Pause", ImVec2(100, 0)))
        {
            switch (bgmstate)
            {
            case BGMstate::stop:
                originalNotes = notes;
                musicTime = editorBeat * (60.0f / bpm);
                sound.SetPlayPositionSeconds(SOUND_LABEL_BGM001, musicTime);
                sound.Play(SOUND_LABEL_BGM001);
                bgmstate = BGMstate::play;
                break;
            case BGMstate::play:
                notes = originalNotes;
                success = 0; miss = 0;
                sound.Pause(SOUND_LABEL_BGM001);
                bgmstate = BGMstate::pause;
                break;
            case BGMstate::pause:
                sound.Resume(SOUND_LABEL_BGM001);
                bgmstate = BGMstate::play;
                break;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("BACK", ImVec2(80, 0)))
        {
            sound.Stop(SOUND_LABEL_BGM001);
            musicTime = 0.0f;
            bgmstate = BGMstate::stop;
            notes = originalNotes;
            success = 0;
            miss = 0;
        }

        float secondsPerBeat = 60.0f / bpm;
        float timelineSeconds = bgmstate == BGMstate::play ? musicTime : editorBeat * secondsPerBeat;
        float songLengthSeconds = totalBeat * secondsPerBeat;

        ImGui::SameLine();
        ImGui::SetNextItemWidth(260.0f);
        bool timeChanged = ImGui::SliderFloat("Time", &timelineSeconds, 0.0f, songLengthSeconds, "%.2f sec");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        timeChanged |= ImGui::InputFloat("sec", &timelineSeconds, 0.1f, 1.0f, "%.2f");

        if (timeChanged)
        {
            if (timelineSeconds < 0.0f) timelineSeconds = 0.0f;
            if (timelineSeconds > songLengthSeconds) timelineSeconds = songLengthSeconds;
            musicTime = timelineSeconds;
            editorBeat = timelineSeconds / secondsPerBeat;

            if (bgmstate == BGMstate::play)
                sound.SeekSeconds(SOUND_LABEL_BGM001, musicTime);
            else
                sound.SetPlayPositionSeconds(SOUND_LABEL_BGM001, musicTime);
        }

        ImGui::SameLine();
        int minutes = (int)(musicTime / 60);
        int seconds = (int)(musicTime) % 60;
        char timeStr[64];
        sprintf_s(timeStr, "%02d:%02d  OK:%d MISS:%d", minutes, seconds, success, miss);
        ImGui::Text(timeStr);

        if (selectedNoteIndex >= 0 && selectedNoteIndex < (int)notes.size())
        {
            Note& n = notes[selectedNoteIndex];
            float duration = n.time - n.GetSpawnTime();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            if (ImGui::InputFloat("Arrive", &n.time, 0.1f, 1.0f, "%.2f"))
            {
                if (n.time <= n.GetSpawnTime()) n.time = n.GetSpawnTime() + 0.01f;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            if (ImGui::InputFloat("Dur", &duration, 0.1f, 1.0f, "%.2f"))
            {
                if (duration < 0.01f) duration = 0.01f;
                n.time = n.GetSpawnTime() + duration;
            }
        }

        ImGui::End();
    }

    m_deviceResources->PIXEndEvent();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    m_deviceResources->Present();
}

// Helper method to clear the back buffers.
void Game::Clear()
{
    m_deviceResources->PIXBeginEvent(L"Clear");

    // Clear the views.
    auto context = m_deviceResources->GetD3DDeviceContext();
    auto renderTarget = m_deviceResources->GetRenderTargetView();
    auto depthStencil = m_deviceResources->GetDepthStencilView();

    context->ClearRenderTargetView(renderTarget, Colors::CornflowerBlue);
    context->ClearDepthStencilView(depthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    context->OMSetRenderTargets(1, &renderTarget, depthStencil);

    // Set the viewport.
    auto const viewport = m_deviceResources->GetScreenViewport();
    context->RSSetViewports(1, &viewport);

    m_deviceResources->PIXEndEvent();
}
#pragma endregion

#pragma region Message Handlers
// Message handlers
void Game::OnActivated()
{
    // TODO: Game is becoming active window.
}

void Game::OnDeactivated()
{
    // TODO: Game is becoming background window.
}

void Game::OnSuspending()
{
    // TODO: Game is being power-suspended (or minimized).
}

void Game::OnResuming()
{
    m_timer.ResetElapsedTime();

    // TODO: Game is being power-resumed (or returning from minimize).
}

void Game::OnWindowMoved()
{
    auto const r = m_deviceResources->GetOutputSize();
    m_deviceResources->WindowSizeChanged(r.right, r.bottom);
}

void Game::OnDisplayChange()
{
    m_deviceResources->UpdateColorSpace();
}

void Game::OnWindowSizeChanged(int width, int height)
{
    if (!m_deviceResources->WindowSizeChanged(width, height))
        return;

    CreateWindowSizeDependentResources();

    // TODO: Game window is being resized.
}

void Game::SaveChart(std::string name)
{
    std::ofstream file("charts/" + name + ".json");
    file << "{\n\"notes\": [\n";

    for (size_t i = 0; i < notes.size(); i++)
    {
        file << "{";
        file << "\"lane\":" << notes[i].lane << ",";
        file << "\"type\":" << notes[i].note << ",";
        file << "\"time\":" << notes[i].time;

        if (notes[i].spawnTime >= 0.0f)
            file << ",\"spawnTime\":" << notes[i].spawnTime;

        file << ",\"spawnPos\":{"
            << "\"x\":" << notes[i].spawnPos.x << ","
            << "\"y\":" << notes[i].spawnPos.y << ","
            << "\"z\":" << notes[i].spawnPos.z << "}";

        file << ",\"judgePos\":{"
            << "\"x\":" << notes[i].judgePos.x << ","
            << "\"y\":" << notes[i].judgePos.y << ","
            << "\"z\":" << notes[i].judgePos.z << "}";

        if (!notes[i].controlPoints.empty())
        {
            file << ",\"path\":[";
            for (size_t p = 0; p < notes[i].controlPoints.size(); p++)
            {
                auto& cp = notes[i].controlPoints[p];
                file << "{\"x\":" << cp.x
                    << ",\"y\":" << cp.y
                    << ",\"z\":" << cp.z << "}";
                if (p != notes[i].controlPoints.size() - 1) file << ",";
            }
            file << "]";
        }

        file << "}";
        if (i != notes.size() - 1) file << ",";
        file << "\n";
    }

    file << "],\n\"beams\":[\n";
    for (size_t i = 0; i < beams.size(); i++)
    {
        file << "{";
        file << "\"lane\":" << beams[i].lane << ",";
        file << "\"startTime\":" << beams[i].startTime << ",";
        file << "\"endTime\":" << beams[i].endTime << ",";
        file << "\"warningTime\":" << beams[i].warningTime;
        file << "}";
        if (i != beams.size() - 1) file << ",";
        file << "\n";
    }
    file << "]\n}\n";
}

void Game::LoadChart(std::string name)
{
    std::ifstream file("charts/" + name + ".json");
    if (!file.is_open()) return;

    notes.clear();
    beams.clear();

    nlohmann::json j;
    file >> j;

    if (j.contains("notes"))
    {
        for (const auto& n : j["notes"])
        {
            Note note;
            note.lane = n["lane"].get<int>();
            note.note = static_cast<NoteType>(n["type"].get<int>());
            note.time = n["time"].get<float>();

            if (n.contains("spawnTime"))
                note.spawnTime = n["spawnTime"].get<float>();

            // spawnPos
            if (n.contains("spawnPos"))
            {
                note.spawnPos.x = n["spawnPos"]["x"].get<float>();
                note.spawnPos.y = n["spawnPos"]["y"].get<float>();
                note.spawnPos.z = n["spawnPos"]["z"].get<float>();
            }
            else
            {
                float laneX = -310.0f + (660.0f / 6.0f) * note.lane + (660.0f / 12.0f);
                note.spawnPos = { laneX, -55.0f, 1000.0f };
            }

            // judgePos
            if (n.contains("judgePos"))
            {
                note.judgePos.x = n["judgePos"]["x"].get<float>();
                note.judgePos.y = n["judgePos"]["y"].get<float>();
                note.judgePos.z = n["judgePos"]["z"].get<float>();
            }
            else
            {
                float laneX = -310.0f + (660.0f / 6.0f) * note.lane + (660.0f / 12.0f);
                note.judgePos = { laneX, -55.0f, -210.0f };
            }

            if (n.contains("path"))
            {
                for (const auto& p : n["path"])
                {
                    DirectX::SimpleMath::Vector3 v;
                    v.x = p["x"].get<float>();
                    v.y = p["y"].get<float>();
                    v.z = p["z"].get<float>();
                    note.controlPoints.emplace_back(v);
                }
            }

            notes.emplace_back(note);
        }

        std::sort(notes.begin(), notes.end(),
            [](const Note& a, const Note& b) {
                return a.time < b.time;
            });
    }

    if (j.contains("beams"))
    {
        for (const auto& b : j["beams"])
        {
            beams.push_back(Beams(
                b["lane"].get<int>(),
                b["startTime"].get<float>(),
                b["endTime"].get<float>(),
                b["warningTime"].get<float>()
            ));
        }
    }
}

//void Game::NoteBorn()
//{
//    POINT p;
//    GetCursorPos(&p);
//    ScreenToClient(GetActiveWindow(), &p);
//
//    if (!isOutRange)
//    {
//        float mouseX = (float)p.x;
//        float mouseY = (float)p.y - 30.0f;
//
//
//        // ?N???b?N??u?????[?????v?Z
//        int clickedLane = (int)((mouseX - chartPosX - laneStartX) / laneWidth);
//        if (clickedLane < 0)
//            clickedLane = 0;
//        else if (clickedLane >= LANE_COUNT)
//            clickedLane = LANE_COUNT - 1;
//
//        // BPM???
//        float secondsPerBeat = 60.0f / bpm;
//
//        // pixelsPerBeat ??O???b?h??1???????
//        float pixelsPerBeat = 100.0f;
//
//
//        // currentBeat ???g???i???????musicTime???Z?A??~????editorBeat?j
//        float clickedBeat = currentBeat + (judgeLine - mouseY) / pixelsPerBeat;
//        float clickedTime = clickedBeat * secondsPerBeat;
//        clickedTime = SnapToGrid(clickedTime, bpm, division);
//        // ?m?[?c???
//        notes.push_back(Note(clickedLane, clickedTime, currentNoteType));
//    }
//
//
//}

void Game::JudgeNoteS()
{
    //????
    if (GetAsyncKeyState('S') & 0x0001)
    {
        int targetLane;
        if (!mirrorMode)
        {
            targetLane = 0; //S???
        }
        else
        {
            targetLane = 5;
        }


        float closeNote = 9999.0f;  //???[???????????m?[?c??T??
        int NoteKind = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;  //?^?[?Q?b?g???[????O??m?[?c???X???[
            if (notes[i].note == LONG_START || notes[i].note == LONG_CENTER) continue; //LONG???X???[

            float note = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(note - judgeLine);

            if (diff < closeNote)
            {
                closeNote = diff;
                NoteKind = i;
            }

            if (NoteKind != -1)
            {
                if (closeNote < 20)
                {
                    printf("PERFECT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    success++;

                    break;
                }
                else if (closeNote < 40)
                {
                    printf("GREAT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
                else if (closeNote < 80)
                {
                    printf("GUD");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }

            }
        }
    }
}

void Game::JudgeNoteD()
{
    //????
    if (GetAsyncKeyState('D') & 0x0001)
    {
        int targetLane; //D???
        if (!mirrorMode)
        {
            targetLane = 1;
        }
        else
        {
            targetLane = 4;
        }

        float closeNote = 9999.0f;  //???[???????????m?[?c??T??
        int NoteKind = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;  //?^?[?Q?b?g???[????O??m?[?c???X???[
            if (notes[i].note == LONG_START || notes[i].note == LONG_CENTER) continue; //LONG???X???[

            float note = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(note - judgeLine);

            if (diff < closeNote)
            {
                closeNote = diff;
                NoteKind = i;
            }

            if (NoteKind != -1)
            {
                if (closeNote < 20)
                {
                    printf("PERFECT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    success++;
                    break;
                }
                else if (closeNote < 40)
                {
                    printf("GREAT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
                else if (closeNote < 80)
                {
                    printf("GUD");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
            }
        }
    }
}

void Game::JudgeNoteF()
{
    //????
    if (GetAsyncKeyState('F') & 0x0001)
    {
        int targetLane; //F???
        if (!mirrorMode)
        {
            targetLane = 2;
        }
        else
        {
            targetLane = 3;
        }

        float closeNote = 9999.0f;  //???[???????????m?[?c??T??
        int NoteKind = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;  //?^?[?Q?b?g???[????O??m?[?c???X???[
            if (notes[i].note == LONG_START || notes[i].note == LONG_CENTER) continue; //LONG???X???[

            float note = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(note - judgeLine);

            if (diff < closeNote)
            {
                closeNote = diff;
                NoteKind = i;
            }

            if (NoteKind != -1)
            {
                if (closeNote < 20)
                {
                    printf("PERFECT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    success++;
                    break;
                }
                else if (closeNote < 40)
                {
                    printf("GREAT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
                else if (closeNote < 80)
                {
                    printf("GUD");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
            }
        }
    }
}

void Game::JudgeNoteJ()
{
    //????
    if (GetAsyncKeyState('J') & 0x0001)
    {
        int targetLane; //J???
        if (!mirrorMode)
        {
            targetLane = 3;
        }
        else
        {
            targetLane = 2;
        }

        float closeNote = 9999.0f;  //???[???????????m?[?c??T??
        int NoteKind = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;  //?^?[?Q?b?g???[????O??m?[?c???X???[
            if (notes[i].note == LONG_START || notes[i].note == LONG_CENTER) continue; //LONG???X???[

            float note = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(note - judgeLine);

            if (diff < closeNote)
            {
                closeNote = diff;
                NoteKind = i;
            }

            if (NoteKind != -1)
            {
                if (closeNote < 20)
                {
                    printf("PERFECT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    success++;
                    break;
                }
                else if (closeNote < 40)
                {
                    printf("GREAT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
                else if (closeNote < 80)
                {
                    printf("GUD");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
            }
        }
    }
}

void Game::JudgeNoteK()
{
    //????
    if (GetAsyncKeyState('K') & 0x0001)
    {
        int targetLane; //K???
        if (!mirrorMode)
        {
            targetLane = 4;
        }
        else
        {
            targetLane = 1;
        }

        float closeNote = 9999.0f;  //???[???????????m?[?c??T??
        int NoteKind = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;  //?^?[?Q?b?g???[????O??m?[?c???X???[
            if (notes[i].note == LONG_START || notes[i].note == LONG_CENTER) continue; //LONG???X???[

            float note = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(note - judgeLine);

            if (diff < closeNote)
            {
                closeNote = diff;
                NoteKind = i;
            }

            if (NoteKind != -1)
            {
                if (closeNote < 20)
                {
                    printf("PERFECT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    success++;
                    break;
                }
                else if (closeNote < 40)
                {
                    printf("GREAT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
                else if (closeNote < 80)
                {
                    printf("GUD");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
            }
        }
    }
}

void Game::JudgeNoteL()
{
    //????
    if (GetAsyncKeyState('L') & 0x0001)
    {
        int targetLane; //L???
        if (!mirrorMode)
        {
            targetLane = 5;
        }
        else
        {
            targetLane = 0;
        }

        float closeNote = 9999.0f;  //???[???????????m?[?c??T??
        int NoteKind = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;  //?^?[?Q?b?g???[????O??m?[?c???X???[
            if (notes[i].note == LONG_START || notes[i].note == LONG_CENTER) continue; //LONG???X???[

            float note = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(note - judgeLine);

            if (diff < closeNote)
            {
                closeNote = diff;
                NoteKind = i;
            }

            if (NoteKind != -1)
            {
                if (closeNote < 20)
                {
                    printf("PERFECT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    success++;
                    break;
                }
                else if (closeNote < 40)
                {
                    printf("GREAT");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
                else if (closeNote < 80)
                {
                    printf("GUD");
                    if (notes[i].note == MIRRORNOTE)
                    {
                        mirrorMode = !mirrorMode;
                    }
                    notes.erase(notes.begin() + i);
                    break;
                }
            }
        }
    }
}


void Game::JudgeLongNoteS()
{
    int targetLane;
    if (!mirrorMode)
    {
        targetLane = 0; //S???
    }
    else
    {
        targetLane = 5;
    }

    bool keyPress = (GetAsyncKeyState('S') & 0x8000);
    bool keyPush = (GetAsyncKeyState('S') & 0x0001);
    static bool prevKeyDown = false;

    // ?????O???
    static bool isHold = false;
    static bool isHoldCenter = false;

    if (bgmstate != BGMstate::play) return;
    if (keyPress)
    {
        float closestDiff = 9999.0f;
        int closestIndex = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_START) continue;

            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(y - judgeLine);

            if (diff < closestDiff)
            {
                closestDiff = diff;
                closestIndex = i;
            }
        }

        if (closestIndex != -1 && closestDiff < 40)
        {
            printf("LONG START\n");
            isHold = true;
            fastLong = true;

            notes.erase(notes.begin() + closestIndex);
            success++;
        }
    }
    if (isHold)
    {
        bool hadCenter = false;
        bool hasRemaining = false;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_CENTER) continue;


            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;

            // ?????C???????CENTER?????
            if (y > judgeLine)
            {
                notes.erase(notes.begin() + i);
                i--;
                success++;
                continue;
            }
        }
    }
    if (!keyPress)
    {
        isHold = false;
        isHoldCenter = false;
        fastLong = false;
    }

    prevKeyDown = keyPush;
}

void Game::JudgeLongNoteD()
{
    int targetLane; //D???
    if (!mirrorMode)
    {
        targetLane = 1;
    }
    else
    {
        targetLane = 4;
    }

    bool keyPress = (GetAsyncKeyState('D') & 0x8000);
    bool keyPush = (GetAsyncKeyState('D') & 0x0001);
    static bool prevKeyDown = false;

    // ?????O???
    static bool isHold = false;
    static bool isHoldCenter = false;

    if (bgmstate != BGMstate::play) return;
    if (keyPress)
    {
        float closestDiff = 9999.0f;
        int closestIndex = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_START) continue;

            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(y - judgeLine);

            if (diff < closestDiff)
            {
                closestDiff = diff;
                closestIndex = i;
            }
        }

        if (closestIndex != -1 && closestDiff < 40)
        {
            printf("LONG START\n");
            isHold = true;
            fastLong = true;

            notes.erase(notes.begin() + closestIndex);
            success++;
        }
    }
    if (isHold)
    {
        bool hadCenter = false;
        bool hasRemaining = false;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_CENTER) continue;


            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;

            // ?????C???????CENTER?????
            if (y > judgeLine)
            {
                notes.erase(notes.begin() + i);
                i--;
                success++;
                continue;
            }
        }
    }
    if (!keyPress)
    {
        isHold = false;
        isHoldCenter = false;
        fastLong = false;
    }

    prevKeyDown = keyPush;
}

void Game::JudgeLongNoteF()
{
    int targetLane; //F???
    if (!mirrorMode)
    {
        targetLane = 2;
    }
    else
    {
        targetLane = 3;
    }

    bool keyPress = (GetAsyncKeyState('F') & 0x8000);
    bool keyPush = (GetAsyncKeyState('F') & 0x0001);
    static bool prevKeyDown = false;

    // ?????O???
    static bool isHold = false;
    static bool isHoldCenter = false;

    if (bgmstate != BGMstate::play) return;
    if (keyPress)
    {
        float closestDiff = 9999.0f;
        int closestIndex = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_START) continue;

            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(y - judgeLine);

            if (diff < closestDiff)
            {
                closestDiff = diff;
                closestIndex = i;
            }
        }

        if (closestIndex != -1 && closestDiff < 40)
        {
            printf("LONG START\n");
            isHold = true;
            fastLong = true;

            notes.erase(notes.begin() + closestIndex);
            success++;
        }
    }
    if (isHold)
    {
        bool hadCenter = false;
        bool hasRemaining = false;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_CENTER) continue;


            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;

            // ?????C???????CENTER?????
            if (y > judgeLine)
            {
                notes.erase(notes.begin() + i);
                i--;
                success++;
                continue;
            }
        }
    }
    if (!keyPress)
    {
        isHold = false;
        isHoldCenter = false;
        fastLong = false;
    }

    prevKeyDown = keyPush;
}

void Game::JudgeLongNoteJ()
{
    int targetLane; //J???
    if (!mirrorMode)
    {
        targetLane = 3;
    }
    else
    {
        targetLane = 2;
    }

    bool keyPress = (GetAsyncKeyState('J') & 0x8000);
    bool keyPush = (GetAsyncKeyState('J') & 0x0001);
    static bool prevKeyDown = false;

    // ?????O???
    static bool isHold = false;
    static bool isHoldCenter = false;

    if (bgmstate != BGMstate::play) return;
    if (keyPress)
    {
        float closestDiff = 9999.0f;
        int closestIndex = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_START) continue;

            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(y - judgeLine);

            if (diff < closestDiff)
            {
                closestDiff = diff;
                closestIndex = i;
            }
        }

        if (closestIndex != -1 && closestDiff < 40)
        {
            printf("LONG START\n");
            isHold = true;
            fastLong = true;

            notes.erase(notes.begin() + closestIndex);
            success++;
        }
    }
    if (isHold)
    {
        bool hadCenter = false;
        bool hasRemaining = false;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_CENTER) continue;


            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;

            // ?????C???????CENTER?????
            if (y > judgeLine)
            {
                notes.erase(notes.begin() + i);
                i--;
                success++;
                continue;
            }
        }
    }
    if (!keyPress)
    {
        isHold = false;
        isHoldCenter = false;
        fastLong = false;
    }

    prevKeyDown = keyPush;
}

void Game::JudgeLongNoteK()
{
    int targetLane; //K???
    if (!mirrorMode)
    {
        targetLane = 4;
    }
    else
    {
        targetLane = 1;
    }

    bool keyPress = (GetAsyncKeyState('K') & 0x8000);
    bool keyPush = (GetAsyncKeyState('K') & 0x0001);
    static bool prevKeyDown = false;

    // ?????O???
    static bool isHold = false;
    static bool isHoldCenter = false;

    if (bgmstate != BGMstate::play) return;
    if (keyPress)
    {
        float closestDiff = 9999.0f;
        int closestIndex = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_START) continue;

            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(y - judgeLine);

            if (diff < closestDiff)
            {
                closestDiff = diff;
                closestIndex = i;
            }
        }

        if (closestIndex != -1 && closestDiff < 40)
        {
            printf("LONG START\n");
            isHold = true;
            fastLong = true;

            notes.erase(notes.begin() + closestIndex);
            success++;
        }
    }
    if (isHold)
    {
        bool hadCenter = false;
        bool hasRemaining = false;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_CENTER) continue;


            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;

            // ?????C???????CENTER?????
            if (y > judgeLine)
            {
                notes.erase(notes.begin() + i);
                i--;
                success++;
                continue;
            }
        }
    }
    if (!keyPress)
    {
        isHold = false;
        isHoldCenter = false;
        fastLong = false;
    }

    prevKeyDown = keyPush;
}

void Game::JudgeLongNoteL()
{
    int targetLane; //L???
    if (!mirrorMode)
    {
        targetLane = 5;
    }
    else
    {
        targetLane = 0;
    }

    bool keyPress = (GetAsyncKeyState('L') & 0x8000);
    bool keyPush = (GetAsyncKeyState('L') & 0x0001);
    static bool prevKeyDown = false;

    // ?????O???
    static bool isHold = false;
    static bool isHoldCenter = false;

    if (bgmstate != BGMstate::play) return;
    if (keyPress)
    {
        float closestDiff = 9999.0f;
        int closestIndex = -1;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_START) continue;

            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;
            float diff = fabs(y - judgeLine);

            if (diff < closestDiff)
            {
                closestDiff = diff;
                closestIndex = i;
            }
        }

        if (closestIndex != -1 && closestDiff < 40)
        {
            printf("LONG START\n");
            isHold = true;
            fastLong = true;

            notes.erase(notes.begin() + closestIndex);
            success++;
        }
    }
    if (isHold)
    {
        bool hadCenter = false;
        bool hasRemaining = false;

        for (int i = 0; i < notes.size(); i++)
        {
            if (notes[i].lane != targetLane) continue;
            if (notes[i].note != LONG_CENTER) continue;


            float y = judgeLine - (notes[i].time - musicTime) * playSpeed;

            if (y > judgeLine)
            {
                notes.erase(notes.begin() + i);
                i--;
                success++;
                continue;
            }
        }
    }
    if (!keyPress)
    {
        isHold = false;
        isHoldCenter = false;
        fastLong = false;
    }

    prevKeyDown = keyPush;
}

bool Game::NoteExit(int lane, float time)
{
    for (const auto& n : notes)
    {
        if (n.lane == lane && fabs(n.time - time) < 0.01f)
        {
            return true;
        }
    }
    return false;
}

int Game::GetMirrorLane(int lane)
{
    if (!mirrorMode) return lane;
    if (mirrorLane == 3)
    {

    }

    //if (mirrored < 0 || mirrored >= LANE_COUNT)
    //{
    //    return -1;
    //}
    //return mirrored;
}

// Properties
void Game::GetDefaultSize(int& width, int& height) const noexcept
{
    // TODO: Change to desired default window size (note minimum size is 320x200).
    width = 1000;
    height = 800;
}
#pragma endregion

#pragma region Direct3D Resources
// These are the resources that depend on the device.
void Game::CreateDeviceDependentResources()
{
    auto device = m_deviceResources->GetD3DDevice();

    m_states = std::make_unique<DirectX::CommonStates>(device);
    m_primitiveBatch =
        std::make_unique<DirectX::PrimitiveBatch<DirectX::VertexPositionColor>>(
            m_deviceResources->GetD3DDeviceContext()
        );

    m_basicEffect = std::make_unique<DirectX::BasicEffect>(device);
    m_basicEffect->SetVertexColorEnabled(true);

    void const* shaderByteCode;
    size_t byteCodeLength;
    m_basicEffect->GetVertexShaderBytecode(&shaderByteCode, &byteCodeLength);

    DX::ThrowIfFailed(
        device->CreateInputLayout(
            DirectX::VertexPositionColor::InputElements,
            DirectX::VertexPositionColor::InputElementCount,
            shaderByteCode,
            byteCodeLength,
            m_inputLayout.ReleaseAndGetAddressOf()
        )
    );

    //CreateWICTextureFromFile(
    //    device,
    //    L"asset/hato_touka.png",
    //    nullptr,
    //    &noteTexture
    //);
    ////?^?b?v
    //CreateWICTextureFromFile(
    //    device,
    //    L"asset/hato_touka.png",
    //    nullptr,
    //    &noteTap
    //);
    ////?????O
    //CreateWICTextureFromFile(
    //    device,
    //    L"asset/ude.png",
    //    nullptr,
    //    &noteLongStart
    //);

    //CreateWICTextureFromFile(
    //    device,
    //    L"asset/Long(aida).png",
    //    nullptr,
    //    &noteLongCenter
    //);

    //CreateWICTextureFromFile(
    //    device,
    //    L"asset/Long(kari).jpg",
    //    nullptr,
    //    &noteLongEnd
    //);
    ////?A???`?~???[
    //CreateWICTextureFromFile(
    //    device,
    //    L"asset/Untimirror(kari).jpg",
    //    nullptr,
    //    &noteUntiMirror
    //);
    ////?~???[?m?[?c?i??????????~???[?I???I?t??????j
    //CreateWICTextureFromFile(
    //    device,
    //    L"asset/block.png",
    //    nullptr,
    //    &noteMirror
    //);
    ////?r?[???i?\??????????????j
    //CreateWICTextureFromFile(
    //    device,
    //    L"asset/beam.png",
    //    nullptr,
    //    &Beam
    //);
}

// Allocate all memory resources that change on a window SizeChanged event.
void Game::CreateWindowSizeDependentResources()
{
    // TODO: Initialize windows-size dependent objects here.
}

float Game::SnapToGrid(float time, float bpm, int division)
{
    float secondBeat = 60.0f / bpm;
    float gridUnit = secondBeat / division;

    return std::round(time / gridUnit) * gridUnit;
}

void Game::OnDeviceLost()
{
    // TODO: Add Direct3D resource cleanup here.
}

void Game::OnDeviceRestored()
{
    CreateDeviceDependentResources();

    CreateWindowSizeDependentResources();
}
#pragma endregion