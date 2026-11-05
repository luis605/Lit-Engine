#include <cstdint>
#include <memory>
#include <vector>

#include "Harness.hpp"

import Engine.Audio;
import Engine.AudioTrace;
import Engine.Geometry;
import Engine.glm;
import Engine.Rng;

namespace bench {

namespace {

constexpr uint32_t kBlockFrames = 512;
constexpr size_t kVoiceCount = 32;
constexpr float kClipSeconds = 8.0f;
constexpr float kBaseFrequency = 110.0f;
constexpr float kVoiceRange = 30.0f;
constexpr size_t kEmitterCount = 64;
constexpr size_t kLargeEmitterCount = 256;
constexpr size_t kLineCount = 32;
constexpr float kLineSeconds = 0.5f;
constexpr uint32_t kPropagationRays = 256;
constexpr int kWallGrid = 4;
constexpr float kWallSpacing = 12.0f;
constexpr float kWallHalfThickness = 0.2f;
constexpr float kWallHalfSize = 4.0f;
constexpr float kRoomHalf = 30.0f;
constexpr float kHalf = 0.5f;
constexpr float kEarHeight = 1.5f;

struct MixerFixture {
    AudioMixer mixer;
    std::vector<float> output = std::vector<float>(static_cast<size_t>(kBlockFrames) * 2);
};

Case mixerCase() {
    auto fixture = std::make_shared<MixerFixture>();
    fixture->mixer.setListener(glm::vec3(0.0f, kEarHeight, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    Pcg32 rng(kSeed + 5);
    for (size_t index = 0; index < kVoiceCount; ++index) {
        VoiceParams params;
        params.clip = AudioClip::sine("tone", kBaseFrequency * static_cast<float>(1 + index % 8), kClipSeconds);
        params.loop = true;
        params.position = glm::vec3((rng.uniform() - kHalf) * kVoiceRange, kEarHeight, (rng.uniform() - kHalf) * kVoiceRange);
        fixture->mixer.play(params);
    }
    fixture->mixer.update();
    Case benchCase;
    benchCase.itemsPerIteration = kBlockFrames;
    benchCase.iterations = 200;
    benchCase.warmup = 10;
    benchCase.run = [fixture] {
        fixture->mixer.update();
        fixture->mixer.render(fixture->output.data(), kBlockFrames);
        sink(fixture->output[0]);
    };
    return benchCase;
}

TriangleSoup buildWalls() {
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -kHalf, 0.0f)), glm::vec3(kRoomHalf, kHalf, kRoomHalf));
    const float origin = -kWallSpacing * static_cast<float>(kWallGrid - 1) * kHalf;
    for (int row = 0; row < kWallGrid; ++row) {
        for (int column = 0; column < kWallGrid; ++column) {
            const glm::vec3 center(origin + kWallSpacing * static_cast<float>(column), kWallHalfSize, origin + kWallSpacing * static_cast<float>(row));
            soup.addBox(glm::translate(glm::mat4(1.0f), center), glm::vec3(kWallHalfThickness, kWallHalfSize, kWallHalfSize));
        }
    }
    return soup;
}

Case propagationCase(size_t emitterCount, uint32_t rays) {
    auto propagation = std::make_shared<AudioPropagation>();
    propagation->setScene(buildWalls());
    propagation->config().raysPerFrame = rays;
    auto emitters = std::make_shared<std::vector<AudioEmitter>>();
    Pcg32 rng(kSeed + 6);
    for (uint32_t index = 0; index < emitterCount; ++index) {
        emitters->push_back({index + 1, glm::vec3((rng.uniform() - kHalf) * kVoiceRange, kEarHeight, (rng.uniform() - kHalf) * kVoiceRange)});
    }
    Case benchCase;
    benchCase.itemsPerIteration = static_cast<double>(emitterCount);
    benchCase.iterations = 60;
    benchCase.warmup = 5;
    benchCase.run = [propagation, emitters] {
        propagation->update(glm::vec3(0.0f, kEarHeight, 0.0f), *emitters);
        sink(static_cast<double>(propagation->raysUsed()));
    };
    return benchCase;
}

struct SchedulerFixture {
    AudioMixer mixer;
    VoiceScheduler scheduler{mixer};
    std::vector<std::shared_ptr<AudioClip>> clips;
    size_t next = 0;
};

Case schedulerCase() {
    auto fixture = std::make_shared<SchedulerFixture>();
    fixture->mixer.setListener(glm::vec3(0.0f, kEarHeight, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    for (size_t index = 0; index < kLineCount; ++index) fixture->clips.push_back(AudioClip::sine("line", kBaseFrequency, kLineSeconds));
    Case benchCase;
    benchCase.itemsPerIteration = 1.0;
    benchCase.iterations = 200;
    benchCase.warmup = 10;
    benchCase.run = [fixture] {
        VoiceLine line;
        line.id = "line";
        line.clip = fixture->clips[fixture->next % kLineCount];
        line.priority = static_cast<int>(fixture->next % 3);
        line.position = glm::vec3(1.0f, kEarHeight, 2.0f);
        ++fixture->next;
        fixture->scheduler.enqueue(std::move(line));
        fixture->scheduler.update(0.0f);
        fixture->mixer.update();
        sink(static_cast<double>(fixture->scheduler.pending()));
    };
    return benchCase;
}

}

void registerAudioBenches() {
    add("audio.render_block_512_32voices", mixerCase);
    add("audio.propagation_update_64emitters", [] { return propagationCase(kEmitterCount, kPropagationRays); });
    add("audio.propagation_update_256emitters", [] { return propagationCase(kLargeEmitterCount, kPropagationRays * 4); });
    add("audio.voice_scheduler", schedulerCase);
}

}
