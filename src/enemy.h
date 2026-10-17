#pragma once
#include "store.h"
#include <random>
#include <set>

namespace fd {
// One schedule and one appearance ledger shared by desktop/taskbar modes.
class EnemySpawner {
public:
    explicit EnemySpawner(unsigned seed = std::random_device{}()) : _rng(seed) {}
    bool Enabled() const { return _enabled; }
    double Interval() const { return _interval; }
    void Enable(bool on) { _enabled = on; _clock = 0; }
    void SetInterval(double seconds) { _interval = seconds > 0 ? seconds : 10; _clock = 0; }
    bool Tick(double dt) {
        if (!_enabled) return false;
        _clock += dt;
        if (_clock < _interval) return false;
        _clock = 0; return true;
    }
    void Clear() { _used.clear(); _clock = 0; }
    void Used(const SkinInfo& skin) { _used.insert(skin.dir); }
    void Invalid(const SkinInfo& skin) { _invalid.insert(skin.dir); }
    int Side(bool taskbar) { return std::uniform_int_distribution<int>(0, taskbar ? 1 : 3)(_rng); }
    int Pick(const std::vector<SkinInfo>& skins, const std::set<std::wstring>& present,
             Faction faction = Faction::Unset, bool automatic = false) {
        std::vector<int> fresh, repeat;
        for (int i = 0; i < (int)skins.size(); ++i) {
            const auto& skin = skins[i];
            if ((faction != Faction::Unset && skin.faction != faction) || present.count(skin.dir) || _invalid.count(skin.dir)) continue;
            repeat.push_back(i);
            if (!_used.count(skin.dir)) fresh.push_back(i);
        }
        if (automatic && fresh.empty()) { Enable(false); return -1; }
        const auto& pool = fresh.empty() ? repeat : fresh;
        return pool.empty() ? -1 : pool[std::uniform_int_distribution<int>(0, (int)pool.size() - 1)(_rng)];
    }
private:
    std::mt19937 _rng;
    std::set<std::wstring> _used, _invalid;
    bool _enabled = false;
    double _interval = 10, _clock = 0;
};
}
