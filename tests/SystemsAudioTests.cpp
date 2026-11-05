#include "TestSupport.hpp"
#include "SystemsSuites.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

import Engine.AI;
import Engine.Nav;
import Engine.Rig;
import Engine.Face;
import Engine.Audio;
import Engine.AudioTrace;
import Engine.Geometry;
import Engine.World;
import Engine.Physics;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Params;
import Engine.glm;
import Engine.Json;
import Engine.Rng;

static std::string tempPath(const std::string& name) { return testsupport::tempFilePath("lit_systems_", name); }

static bool nearlyEqual(float a, float b, float eps = 1.0e-3f) { return std::fabs(a - b) <= eps; }

static std::vector<float> renderMixer(AudioMixer& mixer, uint32_t frames) {
    std::vector<float> out(static_cast<size_t>(frames) * 2);
    mixer.update();
    mixer.render(out.data(), frames);
    return out;
}

static void energy(const std::vector<float>& buf, float& left, float& right) {
    left = 0.0f;
    right = 0.0f;
    for (size_t i = 0; i + 1 < buf.size(); i += 2) {
        left += buf[i] * buf[i];
        right += buf[i + 1] * buf[i + 1];
    }
}

static float peakOf(const std::vector<float>& buf) {
    float p = 0.0f;
    for (const float f : buf) p = std::max(p, std::fabs(f));
    return p;
}

static void testMixer() {
    const auto tone = AudioClip::sine("tone", 440.0f, 2.0f, 0.5f);
    {
        AudioMixer mixer;
        mixer.setListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        VoiceParams p;
        p.clip = tone;
        p.position = glm::vec3(-5.0f, 0.0f, 1.0f);
        mixer.play(p);
        float l;
        float r;
        energy(renderMixer(mixer, 8192), l, r);
        CHECK(r > l * 3.0f);

        AudioMixer centered;
        centered.setListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        centered.setReverbReturn(0.0f);
        p.position = glm::vec3(0.0f, 0.0f, 5.0f);
        centered.play(p);
        energy(renderMixer(centered, 8192), l, r);
        CHECK(std::fabs(l - r) < 0.05f * (l + r));

        AudioMixer far;
        far.setListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        p.position = glm::vec3(0.0f, 0.0f, 20.0f);
        far.play(p);
        float fl;
        float fr;
        energy(renderMixer(far, 8192), fl, fr);
        CHECK(fl + fr < (l + r) * 0.3f);
    }
    {
        VoiceParams p;
        p.clip = tone;
        p.spatial = false;
        p.bus = "sfx";
        AudioMixer mixer;
        mixer.addBus("sfx");
        mixer.play(p);
        const float full = peakOf(renderMixer(mixer, 2048));
        AudioMixer half;
        half.addBus("sfx");
        half.setBusGainDb("sfx", -6.0206f);
        half.play(p);
        const float h = peakOf(renderMixer(half, 2048));
        CHECK(nearlyEqual(h / full, 0.5f, 0.01f));
        AudioMixer muted;
        muted.addBus("sfx");
        muted.setBusMute("sfx", true);
        muted.play(p);
        CHECK(peakOf(renderMixer(muted, 2048)) == 0.0f);
        CHECK(mixer.busLevel("sfx") > 0.1f);
    }
    {
        AudioMixer mixer;
        mixer.addBus("music");
        ParamBus params;
        params.declare("calm", 0.0f);
        mixer.bindBusParam("music", "calm", 0.0f, 1.0f);
        params.snap("calm", 0.25f);
        mixer.update(&params);
        CHECK(nearlyEqual(mixer.busGain("music"), 0.25f));
        params.snap("calm", 1.0f);
        mixer.update(&params);
        CHECK(nearlyEqual(mixer.busGain("music"), 1.0f));
    }
    {
        AudioMixer mixer;
        mixer.addBus("voice");
        mixer.addBus("music");
        mixer.addDuck("voice", "music", -12.0f, 0.01f, 0.01f, 0.2f);
        VoiceParams music;
        music.clip = AudioClip::sine("m", 220.0f, 1.0f, 0.3f);
        music.spatial = false;
        music.bus = "music";
        music.loop = true;
        mixer.play(music);
        VoiceParams voice;
        voice.clip = AudioClip::sine("v", 880.0f, 0.5f, 0.5f);
        voice.spatial = false;
        voice.bus = "voice";
        mixer.play(voice);
        renderMixer(mixer, 19200);
        CHECK(nearlyEqual(mixer.busDuck("music"), 0.251f, 0.05f));
        renderMixer(mixer, 48000 * 2);
        CHECK(mixer.busDuck("music") > 0.95f);
    }
    {
        const auto run = [&](float* peak) {
            AudioMixer mixer;
            mixer.setListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
            VoiceParams p;
            p.clip = AudioClip::noise("n", 1.0f, 9, 0.4f);
            p.position = glm::vec3(-3.0f, 0.0f, 2.0f);
            mixer.play(p);
            NullAudioBackend backend;
            renderAudio(mixer, backend, 1.0f);
            *peak = backend.peak();
            return backend.hash();
        };
        float p1;
        float p2;
        CHECK(run(&p1) == run(&p2) && p1 == p2 && p1 > 0.01f);
    }
    {
        const std::string path = tempPath("render.wav");
        AudioMixer mixer;
        VoiceParams p;
        p.clip = tone;
        p.spatial = false;
        mixer.play(p);
        WavAudioBackend wav(path);
        renderAudio(mixer, wav, 1.0f);
        const auto loaded = AudioClip::loadWav(path);
        CHECK(loaded && loaded->sampleRate == 48000 && loaded->samples.size() == 48000);
        if (loaded) {
            float pk = 0.0f;
            for (const float f : loaded->samples) pk = std::max(pk, std::fabs(f));
            CHECK(pk > 0.4f && pk < 0.6f);
        }
        const std::string mono = tempPath("mono.wav");
        CHECK(tone->saveWav(mono));
        const auto back = AudioClip::loadWav(mono);
        CHECK(back && back->samples.size() == tone->samples.size() && nearlyEqual(back->samples[100], tone->samples[100], 1.0e-3f));
        std::filesystem::remove(path);
        std::filesystem::remove(mono);
    }
}

static void testVoiceScheduler() {
    AudioMixer mixer;
    mixer.setListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    VoiceScheduler sched(mixer);
    std::vector<float> buf(512 * 2);
    const auto advance = [&](int blocks) {
        for (int i = 0; i < blocks; ++i) {
            mixer.render(buf.data(), 512);
            sched.update(512.0f / 48000.0f);
        }
    };
    const auto count = [&](LineEvent type, const std::string& id) {
        return std::count_if(sched.events().begin(), sched.events().end(), [&](const LineEventRecord& e) { return e.type == type && e.id == id; });
    };
    VoiceLine line;
    line.id = "a";
    line.clip = AudioClip::sine("a", 300.0f, 2.0f);
    line.position = glm::vec3(0.0f, 0.0f, 5.0f);
    line.maxDistance = 10.0f;
    line.resumeDistance = 8.0f;
    sched.enqueue(line);
    sched.update(0.0f);
    CHECK(sched.current() && sched.current()->id == "a");
    advance(40);
    const float before = sched.clipTime();
    CHECK(before > 0.3f && before < 1.0f);
    CHECK(sched.energy() > 0.1f);

    mixer.setListener(glm::vec3(0.0f, 0.0f, -30.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    advance(1);
    CHECK(!sched.current());
    CHECK(count(LineEvent::Interrupted, "a") == 1);
    CHECK(mixer.voiceCount() == 0);
    advance(10);
    CHECK(!sched.current());

    mixer.setListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    advance(1);
    CHECK(sched.current() && count(LineEvent::Resumed, "a") == 1);
    float resumedAt = 0.0f;
    for (const LineEventRecord& e : sched.events()) {
        if (e.type == LineEvent::Resumed) resumedAt = e.clipTime;
    }
    CHECK(resumedAt < before && resumedAt > before - 0.4f);

    VoiceLine urgent;
    urgent.id = "b";
    urgent.priority = 5;
    urgent.interruptible = false;
    urgent.positional = false;
    urgent.clip = AudioClip::sine("b", 500.0f, 0.3f);
    sched.enqueue(urgent);
    CHECK(sched.current() && sched.current()->id == "b");
    CHECK(count(LineEvent::Interrupted, "a") == 2);
    VoiceLine low;
    low.id = "c";
    low.positional = false;
    low.clip = AudioClip::sine("c", 600.0f, 0.2f);
    sched.enqueue(low);
    CHECK(sched.current()->id == "b" && sched.pending() == 2);
    advance(40);
    CHECK(count(LineEvent::Finished, "b") == 1);
    CHECK(count(LineEvent::Resumed, "a") == 2);
    advance(200);
    CHECK(count(LineEvent::Finished, "a") == 1 && count(LineEvent::Finished, "c") == 1);
    CHECK(!sched.current() && sched.pending() == 0);

    VoiceLine fragile;
    fragile.id = "d";
    fragile.resumable = false;
    fragile.positional = false;
    fragile.clip = AudioClip::sine("d", 300.0f, 1.0f);
    sched.enqueue(fragile);
    VoiceLine boss;
    boss.id = "e";
    boss.priority = 9;
    boss.positional = false;
    boss.clip = AudioClip::sine("e", 300.0f, 0.1f);
    sched.enqueue(boss);
    CHECK(count(LineEvent::Dropped, "d") == 1 && sched.current()->id == "e");
}

static void testPropagation() {
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(0.2f, 5.0f, 5.0f));
    AudioPropagation prop;
    prop.setScene(soup);
    const glm::vec3 listener(-5.0f, 1.0f, 0.0f);
    prop.beginFrame(listener);
    const AudioPath clear = prop.trace(listener, glm::vec3(-5.0f, 1.0f, 4.0f));
    CHECK(clear.audible && clear.occlusion == 0.0f && nearlyEqual(clear.attenuation, 0.25f, 0.01f));
    const AudioPath blocked = prop.trace(listener, glm::vec3(5.0f, 1.0f, 0.0f));
    CHECK(blocked.directOcclusion == 1.0f && blocked.attenuation < 0.05f && blocked.lowpassHz < 1000.0f && !blocked.viaHop);

    bool portalOpen = true;
    prop.setHopHook([&](const glm::vec3&, std::vector<AudioHop>& out) {
        if (portalOpen) out.push_back({glm::vec3(-0.5f, 1.0f, 3.0f), glm::vec3(0.5f, 1.0f, 3.0f), 0.0f, 7});
    });
    prop.beginFrame(listener);
    const AudioPath hop = prop.trace(listener, glm::vec3(5.0f, 1.0f, 0.0f));
    CHECK(hop.viaHop && hop.hopId == 7 && hop.occlusion == 0.0f && hop.attenuation > blocked.attenuation * 2.0f);
    CHECK(nearlyEqual(hop.arrivalDirection.z, 3.0f / std::sqrt(4.5f * 4.5f + 9.0f), 0.01f) && hop.arrivalDirection.x > 0.7f);
    portalOpen = false;
    prop.beginFrame(listener);
    const AudioPath shut = prop.trace(listener, glm::vec3(5.0f, 1.0f, 0.0f));
    CHECK(!shut.viaHop && shut.directOcclusion == 1.0f);

    prop.setHopHook({});
    prop.config().raysPerFrame = 64;
    std::vector<AudioEmitter> emitters;
    for (uint32_t i = 0; i < 20; ++i) emitters.push_back({i + 1, glm::vec3(-5.0f + static_cast<float>(i) * 0.5f, 1.0f, 3.0f)});
    prop.update(listener, emitters);
    CHECK(prop.raysUsed() <= 64 && prop.staleCount() > 0);
    CHECK(prop.result(1) && !prop.result(1)->stale);
    CHECK(prop.result(20) && prop.result(20)->stale);
    prop.config().raysPerFrame = 512;
    prop.update(listener, emitters);
    CHECK(prop.staleCount() == 0 && prop.raysUsed() <= 512);

    TriangleSoup room;
    room.addBox(glm::mat4(1.0f), glm::vec3(5.0f, 3.0f, 5.0f));
    AudioPropagation enclosed;
    enclosed.setScene(room);
    enclosed.beginFrame(glm::vec3(0.0f));
    AudioPropagation open;
    open.beginFrame(glm::vec3(0.0f));
    CHECK(enclosed.reverb().wet > 0.9f && enclosed.reverb().rt60 > 0.2f && enclosed.reverb().rt60 < 3.0f);
    CHECK(open.reverb().wet == 0.0f);

    AudioPropagation reflect;
    TriangleSoup floorSoup;
    floorSoup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)), glm::vec3(30.0f, 0.5f, 30.0f));
    reflect.setScene(floorSoup);
    reflect.config().reverbRays = 64;
    const glm::vec3 ear(0.0f, 1.5f, 0.0f);
    reflect.beginFrame(ear);
    const AudioPath withBounce = reflect.trace(ear, glm::vec3(4.0f, 1.5f, 0.0f));
    CHECK(!withBounce.reflections.empty() && withBounce.reflections[0].delay > 0.0f && withBounce.reflections[0].gain > 0.0f);

    AudioMixer mixer;
    mixer.setListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    VoiceParams p;
    p.clip = AudioClip::noise("n", 1.0f, 3, 0.4f);
    p.position = glm::vec3(0.0f, 0.0f, 5.0f);
    const VoiceId v = mixer.play(p);
    AudioPath viaLeft;
    viaLeft.audible = true;
    viaLeft.attenuation = 0.5f;
    viaLeft.arrivalDirection = glm::vec3(1.0f, 0.0f, 0.0f);
    mixer.setVoicePath(v, viaLeft);
    float l;
    float r;
    energy(renderMixer(mixer, 4096), l, r);
    CHECK(l > r * 3.0f);
}

void runAudioTests() {
    testMixer();
    testVoiceScheduler();
    testPropagation();
}
