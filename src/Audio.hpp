// Процедурные звуки: всё синтезируется при запуске, файлов не нужно.
#pragma once

#include "raylib.h"

namespace bh {

enum Sfx { SFX_CLICK, SFX_CRIT, SFX_BUY, SFX_NODE, SFX_ACH, SFX_COMET, SFX_WAVE, SFX_FRENZY,
           SFX_COLLAPSE, SFX_DENY, SFX_RANK, SFX_COUNT };

class Audio {
public:
    void init();
    void shutdown();
    void play(Sfx s, float pitch = 1.0f, float volume = 1.0f);
    void update();  // поддерживает фоновый гул
    void toggleMute() { muted_ = !muted_; }
    bool muted() const { return muted_; }

private:
    bool ready_ = false;
    bool muted_ = false;
    Sound sounds_[SFX_COUNT]{};
    Sound drone_{};
};

}  // namespace bh
