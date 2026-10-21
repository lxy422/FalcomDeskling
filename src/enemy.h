#pragma once
#include "store.h"
#include <random>
#include <set>
#include <map>

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
    int Pick(const std::vector<SkinInfo>& skins, const std::map<std::wstring, size_t>& present,
             Faction faction = Faction::Unset, bool automatic = false) {
        std::vector<int> eligible;
        bool canAppearLater = false;
        for (int i = 0; i < (int)skins.size(); ++i) {
            const auto& skin = skins[i];
            if ((faction != Faction::Unset && skin.faction != faction) || _invalid.count(skin.dir)) continue;
            const int limit = skin.RepeatLimit();
            if (limit == 0 && _used.count(skin.dir)) continue;
            canAppearLater = true;
            const auto live = present.find(skin.dir);
            const size_t count = live == present.end() ? 0 : live->second;
            if (count < (size_t)(limit > 0 ? limit : 1)) eligible.push_back(i);
        }
        // A full screen is temporary: keep auto enabled until slots free up.
        // Only exhausted once-only skins (or invalid art) stop the schedule.
        if (automatic && eligible.empty() && !canAppearLater) Enable(false);
        return eligible.empty() ? -1 : eligible[std::uniform_int_distribution<int>(0, (int)eligible.size() - 1)(_rng)];
    }
private:
    std::mt19937 _rng;
    std::set<std::wstring> _used, _invalid;
    bool _enabled = false;
    double _interval = 10, _clock = 0;
};
}
