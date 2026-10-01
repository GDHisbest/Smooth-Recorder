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
int g_videoBitrate = 15000; 
std::string g_resolution = "1080p";
std::string g_videoCodec = "libx264"; 
std::string g_audioCodec = "aac";
int g_audioBitrate = 192; 
float g_audioVolume = 1.0f;
std::string g_customVideoArgs = "-crf 18";
std::string g_customAudioArgs = "";
std::string g_preset = "medium";

float g_fixedDelta = 1.0f / 60.0f;
FFmpegSession* g_ffmpegSession = nullptr;

// Элементы кастомного прогресс-бара рендера
CCLabelBMFont* g_statusLabel = nullptr;
CCLayerColor* g_progressBarBackground = nullptr;
CCLayerColor* g_progressBarFill = nullptr;

// --- Кастомный продвинутый попап настроек ---
class RenderSettingsPopup : public Popup<std::string const&> {
protected:
    bool setup(std::string const& value) override {
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        this->setTitle("Настройки Рендера PRO");

        auto layout = ColumnLayout::create()
            ->setGap(8.f)
            ->setAxisReverse(true)
            ->setAxisAlignment(AxisAlignment::Center);

        auto menu = CCMenu::create();
        menu->setLayout(layout);

        // 1. Выбор Разрешения
        auto resBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(fmt::format("Качество: {}", g_resolution).c_str(), "goldFont.fnt", false, 160.f),
            this, menu_selector(RenderSettingsPopup::onToggleResolution)
        );
        menu->addChild(resBtn);

        // 2. Выбор Кодека
        auto codecBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(fmt::format("Кодек: {}", g_videoCodec).c_str(), "goldFont.fnt", false, 160.f),
            this, menu_selector(RenderSettingsPopup::onToggleCodec)
        );
        menu->addChild(codecBtn);

        // 3. Выбор FPS
        auto fpsBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create(fmt::format("FPS: {}", g_targetFPS).c_str(), "goldFont.fnt", false, 160.f),
            this, menu_selector(RenderSettingsPopup::onToggleFPS)
        );
        menu->addChild(fpsBtn);

        // 4. Громкость
        auto volumeLabel = CCLabelBMFont::create("Громкость Звука:", "chatFont.fnt");
        volumeLabel->setScale(0.5f);
        menu->addChild(volumeLabel);

        auto slider = Slider::create(this, menu_selector(RenderSettingsPopup::onSliderChanged), 0.5f);
        slider->setValue(g_audioVolume);
        menu->addChild(slider);

        // 5. Кнопка "Применить"
        auto saveBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Сохранить", "bigFont.fnt", false, 100.f),
            this, menu_selector(RenderSettingsPopup::onClose)
        );
        menu->addChild(saveBtn);

        menu->updateLayout();
        m_mainLayer->addChild(menu);
        return true;
    }

    void onToggleResolution(CCObject* sender) {
        if (g_resolution == "720p") g_resolution = "1080p";
        else if (g_resolution == "1080p") g_resolution = "2K";
        else if (g_resolution == "2K") g_resolution = "4K";
        else g_resolution = "720p";
        this->refreshUI();
    }

    void onToggleCodec(CCObject* sender) {
        if (g_videoCodec == "libx264") g_videoCodec = "libx265";
        else if (g_videoCodec == "libx265") g_videoCodec = "h264_mediacodec"; 
        else g_videoCodec = "libx264";
        this->refreshUI();
    }

    void onToggleFPS(CCObject* sender) {
        if (g_targetFPS == 30) g_targetFPS = 60;
        else if (g_targetFPS == 60) g_targetFPS = 120;
        else if (g_targetFPS == 120) g_targetFPS = 240;
        else g_targetFPS = 30;
        this->refreshUI();
    }

    void onSliderChanged(CCObject* sender) {
        auto slider = static_cast<Slider*>(sender);
        g_audioVolume = slider->getValue();
    }

    void refreshUI() {
        this->onClose(nullptr);
        RenderSettingsPopup::create()->show();
    }

public:
    static RenderSettingsPopup* create() {
        auto ret = new RenderSettingsPopup();
        if (ret && ret->initAnchored(360.f, 280.f, "Настройки")) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
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
    
    config.width = 1920; config.height = 1080;
    if (g_resolution == "720p") { config.width = 1280; config.height = 720; }
    else if (g_resolution == "2K") { config.width = 2560; config.height = 1440; }
    else if (g_resolution == "4K") { config.width = 3840; config.height = 2160; }

    g_ffmpegSession = FFmpegAPI::startSession(outputPath.string(), config);

    // Инициализируем элементы прогресс-бара на экране
    auto playLayer = PlayLayer::get();
    if (playLayer) {
        auto winSize = CCDirector::sharedDirector()->getWinSize();
        
        // Фон прогресс-бара (черная полоска)
        g_progressBarBackground = CCLayerColor::create(ccc4(0, 0, 0, 150), 200.f, 10.f);
        g_progressBarBackground->setPosition({winSize.width / 2 - 100.f, winSize.height - 25.f});
        
        // Заполнение прогресс-бара (зеленая полоска)
        g_progressBarFill = CCLayerColor::create(ccc4(0, 255, 100, 255), 0.f, 10.f);
        g_progressBarFill->setPosition({winSize.width / 2 - 100.f, winSize.height - 25.f});

        // Текст статуса
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

    // Удаляем элементы прогресс-бара
    if (g_statusLabel) { g_statusLabel->removeFromParent(); g_statusLabel = nullptr; }
    if (g_progressBarBackground) { g_progressBarBackground->removeFromParent(); g_progressBarBackground = nullptr; }
    if (g_progressBarFill) { g_progressBarFill->removeFromParent(); g_progressBarFill = nullptr; }

    FLAlertLayer::create("Smooth Recorder", "Видео успешно сохранено в MP4 формат!", "Отлично")->show();
}

// --- Обновление экрана и Прогресс-бар ---
class $modify(MyScheduler, CCScheduler) {
    void update(float dt) {
        if (g_isRecording) {
            g_fixedDelta = 1.0f / static_cast<float>(g_targetFPS);
            CCScheduler::update(g_fixedDelta);
            this->capturePureFrame();
            this->updateRenderProgressBar();
        } else {
            CCScheduler::update(dt);
        }
    }

    void capturePureFrame() {
        if (!g_ffmpegSession) return;

        auto playLayer = PlayLayer::get();
        bool hudVisibilityState = true;

        if (playLayer && playLayer->m_uiLayer) {
            hudVisibilityState = playLayer->m_uiLayer->isVisible();
            playLayer->m_uiLayer->setVisible(false); 
        }

        // Скрываем элементы нашего прогресс-бара, чтобы они НЕ попали на само видео
        if (g_statusLabel) g_statusLabel->setVisible(false);
        if (g_progressBarBackground) g_progressBarBackground->setVisible(false);
        if (g_progressBarFill) g_progressBarFill->setVisible(false);

        auto director = CCDirector::sharedDirector();
        auto winSize = director->getWinSizeInPixels();
        int width = static_cast<int>(winSize.width);
        int height = static_cast<int>(winSize.height);

        std::vector<GLubyte> buffer(width * height * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data());

        // Возвращаем все элементы обратно на экран устройства
        if (playLayer && playLayer->m_uiLayer) playLayer->m_uiLayer->setVisible(hudVisibilityState);
        if (g_statusLabel) g_statusLabel->setVisible(true);
        if (g_progressBarBackground) g_progressBarBackground->setVisible(true);
        if (g_progressBarFill) g_progressBarFill->setVisible(true);

        FFmpegAPI::writeVideoFrame(g_ffmpegSession, buffer.data(), width, height);
    }

    void updateRenderProgressBar() {
        auto playLayer = PlayLayer::get();
        if (!playLayer || !g_statusLabel || !g_progressBarFill) return;

        // Вычисляем процент прохождения уровня на основе положения игрока
        float length = playLayer->m_levelLength;
        if (length <= 0.f) return;
        
        float currentPosition = playLayer->m_player1->m_position.x;
        float percent = (currentPosition / length) * 100.f;
        if (percent > 100.f) percent = 100.f;
        if (percent < 0.f) percent = 0.f;

        // Обновляем текст и ширину зеленой полоски
        g_statusLabel->setString(fmt::format("Rendering: {:.1f}%", percent).c_str());
        g_progressBarFill->changeWidth(200.f * (percent / 100.f));
    }
};
