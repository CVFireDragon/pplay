//
// Created by cpasjuste on 29/09/18.
//

#include "c2dui.h"
#include "player.h"
#include "kitchensink/kitchensink.h"

#define AUDIOBUFFER_SIZE (1024 * 64)
#define ATLAS_WIDTH 4096
#define ATLAS_HEIGHT 4096
#define ATLAS_MAX 1024

using namespace c2d;
using namespace c2dui;

static UIMain *uiMain = nullptr;

static Kit_Source *src = nullptr;
static Kit_Player *player = nullptr;
static Kit_PlayerInfo player_info;
static SDL_AudioSpec wanted_spec, audio_spec;
static SDL_AudioDeviceID audio_dev;
static char audiobuf[AUDIOBUFFER_SIZE];

Player::Player(UIMain *ui) : UIEmu(ui) {

    printf("Player()\n");
    uiMain = getUi();
}

int Player::run(RomList::Rom *rom) {

    int err = Kit_Init(KIT_INIT_ASS);
    if (err != 0) {
        fprintf(stderr, "Unable to initialize Kitchensink: %s\n", Kit_GetError());
        stop();
        return 1;
    }

    // Open up the sourcefile.
    std::string file = std::string(*getUi()->getConfig()->getRomPath(0) + rom->path);
    src = Kit_CreateSourceFromUrl(file.c_str());
    if (src == nullptr) {
        fprintf(stderr, "Unable to load file '%s': %s\n", file.c_str(), Kit_GetError());
        stop();
        return 1;
    }

    // Create the player. Pick best video, audio and subtitle streams, and set subtitle
    // rendering resolution to screen resolution.
    player = Kit_CreatePlayer(
            src,
            Kit_GetBestSourceStream(src, KIT_STREAMTYPE_VIDEO),
            Kit_GetBestSourceStream(src, KIT_STREAMTYPE_AUDIO),
            -1,
            //Kit_GetBestSourceStream(src, KIT_STREAMTYPE_SUBTITLE),
            (int) uiMain->getRenderer()->getSize().x, (int) uiMain->getRenderer()->getSize().y);
    if (player == nullptr) {
        fprintf(stderr, "Unable to create player: %s\n", Kit_GetError());
        stop();
        return 1;
    }

    // Print some information;
    Kit_GetPlayerInfo(player, &player_info);
    // Make sure there is video in the file to play first.
    if (Kit_GetPlayerVideoStream(player) == -1) {
        fprintf(stderr, "File contains no video!\n");
        stop();
        return 1;
    }

    printf("video(%s, %s): %i x %i , audio(%s): %i\n",
           player_info.video.codec.name,
           SDL_GetPixelFormatName(player_info.video.output.format),
           player_info.video.output.width, player_info.video.output.height,
           player_info.audio.codec.name,
           player_info.audio.output.samplerate);

    // TODO
    //addAudio(player_info.audio.output.samplerate, 30);
    //addVideo(uiMain, nullptr, nullptr, Vector2f(pinfo.video.output.width, pinfo.video.output.height));

    // Init audio
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        SDL_InitSubSystem(SDL_INIT_AUDIO);
    }
    SDL_memset(&wanted_spec, 0, sizeof(wanted_spec));
    wanted_spec.freq = player_info.audio.output.samplerate;
    wanted_spec.format = player_info.audio.output.format;
    wanted_spec.channels = player_info.audio.output.channels;
    audio_dev = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &audio_spec, 0);
    SDL_PauseAudioDevice(audio_dev, 0);

    C2DUIVideo *video = new C2DUIVideo(
            getUi(), nullptr, nullptr,
            {player_info.video.output.width, player_info.video.output.height},
            C2D_TEXTURE_FMT_ABGR8);
    addVideo(video);

    // Start playback
    Kit_PlayerPlay(player);

    return UIEmu::run(rom);
}

void Player::stop() {

    if (player) {
        Kit_PlayerStop(player);
        Kit_ClosePlayer(player);
        player = nullptr;
    }

    if (src) {
        Kit_CloseSource(src);
        src = nullptr;
    }

    Kit_Quit();

    if (audio_dev) {
        SDL_CloseAudioDevice(audio_dev);
    }

    UIEmu::stop();
}

void Player::pause() {

    UIEmu::pause();
}

void Player::resume() {

    UIEmu::resume();
}

int Player::update() {

    // fps
    int showFps = getUi()->getConfig()->getValue(Option::Index::ROM_SHOW_FPS, true);
    getFpsText()->setVisibility(showFps ? Visible : Hidden);
    if (showFps) {
        sprintf(getFpsString(), "FPS: %.2g/%2d", getUi()->getRenderer()->getFps(), 60);
        getFpsText()->setString(getFpsString());
    }

    c2d::Input::Player *players = getUi()->getInput()->update();

    // look for player 1 menu combo
    if (((players[0].state & c2d::Input::Key::KEY_START) && (players[0].state & c2d::Input::Key::KEY_COIN))) {
        pause();
        return UI_KEY_SHOW_MEMU_ROM;
    } else if (((players[0].state & c2d::Input::Key::KEY_START) && (players[0].state & c2d::Input::Key::KEY_FIRE5))
               || ((players[0].state & c2d::Input::Key::KEY_COIN) && (players[0].state & c2d::Input::Key::KEY_FIRE5))
               || ((players[0].state & c2d::Input::Key::KEY_START) && (players[0].state & c2d::Input::Key::KEY_FIRE6))
               || ((players[0].state & c2d::Input::Key::KEY_COIN) && (players[0].state & c2d::Input::Key::KEY_FIRE6))) {
        pause();
        return UI_KEY_SHOW_MEMU_ROM;
    }

    // look for window resize event
    if (players[0].state & EV_RESIZE) {
        // useful for sdl resize event
        getVideo()->updateScaling();
    }

    if (players[0].state & EV_QUIT) {
        stop();
        return EV_QUIT;
    }

    if (!isPaused()) {
#if 0
        auto *aud = (SDL2Audio *) getAudio();
        int buffer_size = getAudio()->getBufferSize();
        int queued = SDL_GetQueuedAudioSize(aud->getDeviceID());

        if (queued < buffer_size) {
            int need = buffer_size - queued;
            while (need > 0) {
                int ret = Kit_GetPlayerAudioData(
                        player,
                        (unsigned char *) getAudio()->getBuffer(),
                        buffer_size);
                need -= ret;
                if (ret > 0) {
                    SDL_QueueAudio(aud->getDeviceID(), getAudio()->getBuffer(), ret);
                } else {
                    break;
                }
            }
            // If we now have data, start playback (again)
            if (SDL_GetQueuedAudioSize(aud->getDeviceID()) > 0) {
                SDL_PauseAudioDevice(aud->getDeviceID(), 0);
            }
        }
#endif
        // audio
        int queued = SDL_GetQueuedAudioSize(audio_dev);
        if (queued < AUDIOBUFFER_SIZE) {
            int need = AUDIOBUFFER_SIZE - queued;

            while (need > 0) {
                int ret = Kit_GetPlayerAudioData(
                        player,
                        (unsigned char *) audiobuf,
                        AUDIOBUFFER_SIZE);
                need -= ret;
                if (ret > 0) {
                    SDL_QueueAudio(audio_dev, audiobuf, ret);
                } else {
                    break;
                }
            }
            // If we now have data, start playback (again)
            if (SDL_GetQueuedAudioSize(audio_dev) > 0) {
                SDL_PauseAudioDevice(audio_dev, 0);
            }
        }

        void *video_data;
        getVideo()->lock(nullptr, &video_data, nullptr);
        Kit_GetPlayerVideoDataRaw(player, video_data);
        getVideo()->unlock();

        getUi()->getRenderer()->flip();
    }

    return 0;
}