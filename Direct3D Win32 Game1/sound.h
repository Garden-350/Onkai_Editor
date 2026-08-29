#pragma once

#include <xaudio2.h>
#include <string>
#include <vector>

// サウンドファイル
typedef enum
{
	SOUND_LABEL_SE001,
	SOUND_LABEL_SE002,
	SOUND_BEAM_SARCH,
	SOUND_BEAM_DECITION,
	SOUND_BEAM_FIRING,

	SOUND_LABEL_BGM001,
	SOUND_LABEL_BGM002,

	SOUND_LABEL_MAX,
} SOUND_LABEL;

class Sound {
private:
	// パラメータ構造体
	typedef struct
	{
		LPCSTR filename;	// 音声ファイルまでのパスを設定
		bool bLoop;			// trueでループ。通常BGMはtrue、SEはfalse。
	} PARAM;

	PARAM m_param[SOUND_LABEL_MAX] =
	{
		//{"asset/BGM/sample000.wav", true},	// サンプルBGM（ループさせるのでtrue設定）
		//{"asset/BGM/○○○.wav", true},	// サンプルBGM
  		// サンプルSE（ループしないのでfalse設定）

		{"asset/se/メトロノームのカチッ.wav",false},
		{"asset/se/メトロノームのチーン.wav", false},
		{"asset/se/beam_sarch.mp3",false},
		{"asset/se/beam_kakutei.mp3",false},
		{"asset/se/beam_Go.wav",false},

		{"asset/bgm/mirror.wav", true},
		{"asset/bgm/maou_bgm_piano39.wav", true},
	};

	IXAudio2* m_pXAudio2 = NULL;
	IXAudio2MasteringVoice* m_pMasteringVoice = NULL;
	IXAudio2SourceVoice* m_pSourceVoice[SOUND_LABEL_MAX];
	WAVEFORMATEXTENSIBLE m_wfx[SOUND_LABEL_MAX]; // WAVフォーマット
	XAUDIO2_BUFFER m_buffer[SOUND_LABEL_MAX];
	BYTE* m_DataBuffer[SOUND_LABEL_MAX];

	UINT32 m_playPos[SOUND_LABEL_MAX] = { 0 };
	IXAudio2SourceVoice* m_pDynamicVoice = nullptr;
	WAVEFORMATEXTENSIBLE  m_dynamicWfx = {};
	XAUDIO2_BUFFER        m_dynamicBuffer = {};
	BYTE* m_dynamicData = nullptr;
	UINT32                m_dynamicPlayPos = 0;
	bool                  m_dynamicLoop = true;
	std::string           m_dynamicLoadedPath;

	HRESULT FindChunk(HANDLE, DWORD, DWORD&, DWORD&);
	HRESULT ReadChunkData(HANDLE, void*, DWORD, DWORD);
	HRESULT LoadMp3AsPCM(LPCSTR filename, WAVEFORMATEXTENSIBLE& outWfx, BYTE*& outData, DWORD& outSize);

public:
	// ゲームループ開始前に呼び出すサウンドの初期化処理
	HRESULT Init(void);

	// ゲームループ終了後に呼び出すサウンドの解放処理
	void Uninit(void);

	// 引数で指定したサウンドを再生する
	void Play(SOUND_LABEL label);

	// 引数で指定したサウンドを停止する
	void Stop(SOUND_LABEL label);

	void Pause(SOUND_LABEL label);

	// 引数で指定したサウンドの再生を再開する
	void Resume(SOUND_LABEL label);

	void SetPlayPositionSeconds(SOUND_LABEL label, float seconds);
	void SeekSeconds(SOUND_LABEL label, float seconds);

	void SetVolume(SOUND_LABEL label, float volume);

	HRESULT LoadDynamic(const std::string& path, bool loop = true);
	void PlayDynamic();
	void StopDynamic();
	void PauseDynamic();
	void ResumeDynamic();
	void SetPlayPositionSecondsDynamic(float seconds);
	void SeekSecondsDynamic(float seconds);
	void SetVolumeDynamic(float volume);
	void ComputeEnvelope(int bucketsPerSecond, std::vector<float>& outLevels) const;
	float GetDynamicDurationSeconds() const;
};