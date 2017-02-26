﻿#include "pch.h"
#include "PPSSPP_UWPMain.h"

#include <mutex>

#include "Common/FileUtil.h"
#include "Common/Log.h"
#include "Common/LogManager.h"
#include "Core/System.h"
#include "base/NativeApp.h"
#include "input/input_state.h"
#include "file/vfs.h"
#include "file/zip_read.h"
#include "file/file_util.h"
#include "base/display.h"
#include "util/text/utf8.h"
#include "Common/DirectXHelper.h"
#include "XAudioSoundStream.h"

using namespace UWP;
using namespace Windows::Foundation;
using namespace Windows::Storage;
using namespace Windows::System::Threading;
using namespace Concurrency;

namespace UWP {

// TODO: Use Microsoft::WRL::ComPtr<> for D3D11 objects?
// TODO: See https://github.com/Microsoft/Windows-universal-samples/tree/master/Samples/WindowsAudioSession for WASAPI with UWP

// Loads and initializes application assets when the application is loaded.
PPSSPP_UWPMain::PPSSPP_UWPMain(const std::shared_ptr<DX::DeviceResources>& deviceResources) :
	m_deviceResources(deviceResources)
{
	// Register to be notified if the Device is lost or recreated
	m_deviceResources->RegisterDeviceNotify(this);

	// create_task(KnownFolders::GetFolderForUserAsync(nullptr, KnownFolderId::RemovableDevices)).then([this](StorageFolder ^));

	// TODO: Change the timer settings if you want something other than the default variable timestep mode.
	// e.g. for 60 FPS fixed timestep update logic, call:
	/*
	m_timer.SetFixedTimeStep(true);
	m_timer.SetTargetElapsedSeconds(1.0 / 60);
	*/

	m_graphicsContext.reset(new UWPGraphicsContext(deviceResources));

	const std::string &exePath = File::GetExeDirectory();
	VFSRegister("", new DirectoryAssetReader((exePath + "/Content/").c_str()));
	VFSRegister("", new DirectoryAssetReader(exePath.c_str()));

	wchar_t lcCountry[256];

	std::string langRegion;
	if (0 != GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SNAME, lcCountry, 256)) {
		langRegion = ConvertWStringToUTF8(lcCountry);
		for (size_t i = 0; i < langRegion.size(); i++) {
			if (langRegion[i] == '-')
				langRegion[i] = '_';
		}
	} else {
		langRegion = "en_US";
	}


	char configFilename[MAX_PATH] = { 0 };
	char controlsConfigFilename[MAX_PATH] = { 0 };

	// On Win32 it makes more sense to initialize the system directories here 
	// because the next place it was called was in the EmuThread, and it's too late by then.
	InitSysDirectories();

	// Load config up here, because those changes below would be overwritten
	// if it's not loaded here first.
	g_Config.AddSearchPath("");
	g_Config.AddSearchPath(GetSysDirectory(DIRECTORY_SYSTEM));
	g_Config.SetDefaultPath(GetSysDirectory(DIRECTORY_SYSTEM));
	g_Config.Load(configFilename, controlsConfigFilename);

	bool debugLogLevel = false;

	g_Config.iGPUBackend = GPU_BACKEND_DIRECT3D11;

#ifdef _DEBUG
	g_Config.bEnableLogging = true;
#endif

	LogManager::Init();

	if (debugLogLevel)
		LogManager::GetInstance()->SetAllLogLevels(LogTypes::LDEBUG);

	const char *argv[2] = { "fake", nullptr };

	NativeInit(1, argv, "", "", "", false);

	NativeInitGraphics(m_graphicsContext.get());
	NativeResized();
	m_graphicsContext->GetDrawContext()->HandleEvent(Draw::Event::GOT_BACKBUFFER);
}

PPSSPP_UWPMain::~PPSSPP_UWPMain() {
	m_graphicsContext->GetDrawContext()->HandleEvent(Draw::Event::LOST_BACKBUFFER);
	NativeShutdownGraphics();
	NativeShutdown();

	// Deregister device notification
	m_deviceResources->RegisterDeviceNotify(nullptr);
}

// Updates application state when the window size changes (e.g. device orientation change)
void PPSSPP_UWPMain::CreateWindowSizeDependentResources() {
	// TODO: Replace this with the size-dependent initialization of your app's content.
	NativeResized();
}

// Updates the application state once per frame.
void PPSSPP_UWPMain::Update() {
	InputState input{};
	NativeUpdate(input);
}

// Renders the current frame according to the current application state.
// Returns true if the frame was rendered and is ready to be displayed.
bool PPSSPP_UWPMain::Render() {
	auto context = m_deviceResources->GetD3DDeviceContext();

	// Reset the viewport to target the whole screen.
	auto viewport = m_deviceResources->GetScreenViewport();

	m_deviceResources->GetBackBufferRenderTargetView()
	pixel_xres = viewport.Width;
	pixel_yres = viewport.Height;

	g_dpi = m_deviceResources->GetDpi();
	g_dpi_scale = 96.0f / g_dpi;

	pixel_in_dps = 1.0f / g_dpi_scale;

	dp_xres = pixel_xres * g_dpi_scale;
	dp_yres = pixel_yres * g_dpi_scale;

	context->RSSetViewports(1, &viewport);

	// Reset render targets to the screen.
	ID3D11RenderTargetView *const targets[1] = { m_deviceResources->GetBackBufferRenderTargetView() };
	context->OMSetRenderTargets(1, targets, m_deviceResources->GetDepthStencilView());

	// Clear the back buffer and depth stencil view.
	context->ClearRenderTargetView(m_deviceResources->GetBackBufferRenderTargetView(), DirectX::Colors::CornflowerBlue);
	context->ClearDepthStencilView(m_deviceResources->GetDepthStencilView(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	NativeRender(m_graphicsContext.get());
	return true;
}

// Notifies renderers that device resources need to be released.
void PPSSPP_UWPMain::OnDeviceLost() {

}

// Notifies renderers that device resources may now be recreated.
void PPSSPP_UWPMain::OnDeviceRestored()
{
	CreateWindowSizeDependentResources();

}

UWPGraphicsContext::UWPGraphicsContext(std::shared_ptr<DX::DeviceResources> resources) {
	ctx_ = Draw::T3DCreateD3D11Context(resources->GetD3DDevice(), resources->GetD3DDeviceContext(), resources->GetD3DDevice(), resources->GetD3DDeviceContext(), 0);
}

void UWPGraphicsContext::Shutdown() {
	delete ctx_;
}

void UWPGraphicsContext::SwapInterval(int interval) {
	
}

}  // namespace UWP

std::string System_GetProperty(SystemProperty prop) {
	static bool hasCheckedGPUDriverVersion = false;
	switch (prop) {
	case SYSPROP_NAME:
		return "Windows 10";
	case SYSPROP_LANGREGION:
		return "en_US";  // TODO UWP
	case SYSPROP_CLIPBOARD_TEXT:
		return "";
	case SYSPROP_GPUDRIVER_VERSION:
		return "";
	default:
		return "";
	}
}

int System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_AUDIO_SAMPLE_RATE:
		return 48000; //winAudioBackend ? winAudioBackend->GetSampleRate() : -1;
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return 60000;
	case SYSPROP_DEVICE_TYPE:
		return DEVICE_TYPE_DESKTOP;
	default:
		return -1;
	}
}

void System_SendMessage(const char *command, const char *parameter) {
	// TODO UWP
}

void LaunchBrowser(const char *url) {
	// TODO UWP
}

void Vibrate(int length_ms) {
	// Ignore on PC
}

void System_AskForPermission(SystemPermission permission) {}

PermissionStatus System_GetPermissionStatus(SystemPermission permission) {
	return PERMISSION_STATUS_GRANTED;
}

bool System_InputBoxGetString(const char *title, const char *defaultValue, char *outValue, size_t outLength) {
	return false;
}

bool System_InputBoxGetWString(const wchar_t *title, const std::wstring &defaultvalue, std::wstring &outvalue) {
	return false;
}
