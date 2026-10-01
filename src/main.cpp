#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <cocos2d.h>
#include <sstream>
#include <string>
#include <vector>

// Подключение официального API от Eclipse Team
#include <eclipse.ffmpeg_api/include/FFmpegAPI.hpp> 

using namespace geode::prelude;

// --- Глобальное состояние рендера ---
bool g_isRecording = false;
int g_targetFPS = 60;                         
std::string g_customFPS = "60";               
int g_videoBitrate = 15000; 
std::string g_customVideoBitrate = "15000";   
std::string g_customResolution = "1920x1080"; 
std::string g_videoCodec = "libx264";         
std::string g_audioCodec = "aac";             
std::string g_customAudioBitrate = "192";     
int g_audioBitrate = 192; 
float g_audioVolume = 1.0f;                   
std::string g_customVideoArgs = "-crf 18";     
std::string g_customAudioArgs = "";           

// Список доступных пресетов FFmpeg для циклического выбора
std::vector<std::string> g_presetsList = {
    "ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"
};
size_t g_currentPresetIndex = 5; // По умолчанию "medium"
std::string g_preset = "medium";

float g_fixedDelta = 1.0f / 60.0f;
FFmpegSession* g_ffmpegSession = nullptr;

// Элементы кастомного прогресс-бара рендера
CCLabelBMFont* g_statusLabel = nullptr;
CCLayerColor* g_progressBarBackground = nullptr;
CCLayerColor* g_progressBarFill = nullptr;

// --- Кастомный продвинутый попап настроек ---
class RenderSettingsPopup : public Popup<std::string const&>, public TextInputDelegate {
protected:
    TextInput* m_resInput = nullptr;
    TextInput* m_vCodecInput = nullptr;
    TextInput* m_fpsInput = nullptr;
    TextInput* m_vBitrateInput = nullptr;
    TextInput* m_aCodecInput = nullptr;
    TextInput* m_aBitrateInput = nullptr;
    TextInput* m_vArgsInput = nullptr;
    TextInput* m_aArgsInput = nullptr;
    CCMenuItemSpriteExtra* m_presetBtn = nullptr;

    bool setup(std::string const& value) override {
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        this->setTitle("Настройки Рендера PRO");

        // Создаем ScrollLayer для прокрутки параметров
        auto scroll = ScrollLayer::create({340.f, 220.f});
        scroll->setPosition({winSize.width / 2 - 170.f, winSize.height / 2 - 120.f});
        
        auto menu = CCMenu::create();
        menu->setPosition({170.f, 0.f});

        auto layout = ColumnLayout::create()
            ->setGap(6.f)
            ->setAxisReverse(true)
            ->setAxisAlignment(AxisAlignment::Center);
        menu->setLayout(layout);

        auto addInputField = [&](TextInput*& inputVar, const std::string& labelText, const std::string& defaultVal, const std::string& allowedChars = "") {
            auto label = CCLabelBMFont::create(labelText.c_str(), "chatFont.fnt");
            label->setScale(0.45f);
            menu->addChild(label);

            inputVar = TextInput::create(180.f, defaultVal.c_str(), "chatFont.fnt");
            inputVar->setString(defaultVal);
            inputVar->setDelegate(this);
            if (!allowedChars.empty()) inputVar->setFilter(allowedChars);
            menu->addChild(inputVar);
        };

        // Текстовые поля ввода
        addInputField(m_resInput, "Разрешение (ШиринаxВысота):", g_customResolution);
        addInputField(m_vCodecInput, "Видео Кодек (например, libx264):", g_videoCodec);
        addInputField(m_fpsInput, "Кадры в секунду (FPS):", g_customFPS, "0123456789");
        addInputField(m_vBitrateInput, "Битрейт Видео (kbps):", g_customVideoBitrate, "0123456789");
        addInputField(m_aCodecInput, "Аудио Кодек (например, aac):", g_audioCodec);
        addInputField(m_aBitrateInput, "Битрейт Аудио (kbps):", g_customAudioBitrate, "0123456789");
        addInputField(m_vArgsInput, "Доп. аргументы Видео:", g_customVideoArgs);
        addInputField(m_aArgsInput, "Доп. аргументы Аудио:", g_customAudioArgs);

        // Кнопка выбора пресета из списка (вместо текстового поля)
        auto presetLabel = CCLabelBMFont::create("Скорость/Качество (Preset):", "chatFont.fnt");
        presetLabel->setScale(0.45f);
        menu->addChild(presetLabel);

        m_presetBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(g_preset.c_str(), "goldFont.fnt", false, 180.f),
            this, menu_selector(RenderSettingsPopup::onTogglePreset)
        );
        menu->addChild(m_presetBtn);

        // Громкость звука (Ползунок)
        auto volumeLabel = CCLabelBMFont::create("Громкость Звука на видео:", "chatFont.fnt");
        volumeLabel->setScale(0.45f);
        menu->addChild(volumeLabel);

        auto slider = Slider::create(this, menu_selector(RenderSettingsPopup::onSliderChanged), 0.5f);
        slider->setValue(g_audioVolume);
        menu->addChild(slider);

        // Обновляем структуру элементов контента
        menu->updateLayout();
        
        float totalHeight = menu->getScaledContentSize().height + 20.f;
        menu->setPositionY(totalHeight - 20.f);
        scroll->m_contentLayer->setContentSize({340.f, totalHeight});
        scroll->m_contentLayer->addChild(menu);
        
        m_mainLayer->addChild(scroll);

        // Кнопка сохранения внизу попапа
        auto saveBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Сохранить изменения", "bigFont.fnt", false, 140.f),
            this, menu_selector(RenderSettingsPopup::onSaveAndClose)
        );
        saveBtn->setScale(0.8f);
        
        auto saveMenu = CCMenu::create();
        saveMenu->setPosition({winSize.width / 2, winSize.height / 2 - 125.f});
        saveMenu->addChild(saveBtn);
        m_mainLayer->addChild(saveMenu);

        return true;
    }

    // Метод циклического переключения пресетов по клику
    void onTogglePreset(CCObject* sender) {
        this->saveInputValues(); // Сохраняем введенный текст перед обновлением UI

        g_currentPresetIndex = (g_currentPresetIndex + 1) % g_presetsList.size();
        g_preset = g_presetsList[g_currentPresetIndex];
        
        // Перерисовываем попап для обновления текста на кнопке пресета
        this->onClose(nullptr);
        RenderSettingsPopup::create()->show();
    }

    void onSliderChanged(CCObject* sender) {
        auto slider = static_cast<Slider*>(sender);
        g_audioVolume = slider->getValue();
    }

    void saveInputValues() {
        if (m_resInput) g_customResolution = m_resInput->getString();
        if (m_vCodecInput) g_videoCodec = m_vCodecInput->getString();
        if (m_aCodecInput) g_audioCodec = m_aCodecInput->getString();
        if (m_vArgsInput) g_customVideoArgs = m_vArgsInput->getString();
        if (m_aArgsInput) g_customAudioArgs = m_aArgsInput->getString();
        
        auto parseSafely = [](TextInput* input, std::string& fallbackStr, int& targetVar, int defaultVal) {
            if (input) {
                fallbackStr = input->getString();
                try {
                    int val = std::stoi(fallbackStr);
                    targetVar = (val > 0) ? val : defaultVal;
                    if (val <= 0) fallbackStr = std::to_string(defaultVal);
                } catch (...) {
                    targetVar = defaultVal;
                    fallbackStr = std::to_string(defaultVal);
                }
            }
        };

        parseSafely(m_fpsInput, g_customFPS, g_targetFPS, 60);
        parseSafely(m_vBitrateInput, g_customVideoBitrate, g_videoBitrate, 15000);
        parseSafely(m_aBitrateInput, g_customAudioBitrate, g_audioBitrate, 192);
    }

    void onSaveAndClose(CCObject* sender) {
        this->saveInputValues();
        this->onClose(sender);
    }
};

// --- Управление рендером ---
void startRecordingProcess() {
    if (g_isRecording) return;
    g_isRecording = true;

    auto outputPath = Mod::get()->getSaveDir() / "render_output.mp4";

    FFmpegConfig config;
    config.fps = g_targetFPS;
    config.videoBitrate = g_videoBitrate;
    config.videoCodec = g_videoCodec;
    config.audioCodec = g_audioCodec;
    config.audioBitrate = g_audioBitrate;
    config.preset = g_preset;
    config.customVideoArgs = g_customVideoArgs;
    config.customAudioArgs = g_customAudioArgs;
    
    int width = 1920; int height = 1080;
    std::stringstream ss(g_customResolution);
    char x;
    if (ss >> width >> x >> height) {
        config.width = width; config.height = height;
    } else {
        config.width = 1920; config.height = 1080;
    }

    g_ffmpegSession = FFmpegAPI::startSession(outputPath.string(), config);

    auto playLayer = PlayLayer::get();
    if (playLayer) {
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        
        g_progressBarBackground = CCLayerColor::create(ccc4(0, 0, 0, 150), 200.f, 10.f);
        g_progressBarBackground->setPosition({winSize.width / 2 - 100.f, winSize.height - 25.f});
        
        g_progressBarFill = CCLayerColor::create(ccc4(0, 255, 100, 255), 0.f, 10.f);
        g_progressBarFill->setPosition({winSize.width / 2 - 100.f, winSize.height - 25.f});

        g_statusLabel = CCLabelBMFont::create("Rendering: 0%", "chatFont.fnt");
        g_statusLabel->setScale(0.5f);
        g_statusLabel->setPosition({winSize.width / 2, winSize.height - 40.f});

        playLayer->addChild(g_progressBarBackground, 101);
        playLayer->addChild(g_progressBarFill, 102);
        playLayer->addChild(g_statusLabel, 103);
    }

    Notification::create("Рендеринг запущен!", NotificationIcon::Success)->show();
}

void stopRecordingProcess() {
    if (!g_isRecording) return;
    g_isRecording = false;

    if (g_ffmpegSession) {
        FFmpegAPI::stopSession(g_ffmpegSession);
        g_ffmpegSession = nullptr;
    }

    if (g_statusLabel) { g_statusLabel->removeFromParent(); g_statusLabel = nullptr; }
