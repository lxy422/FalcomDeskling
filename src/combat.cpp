#include "runner.h"
#include <algorithm>
#include <cmath>

namespace fd {
Art Runner::ResolveArt(Art art) const {
    if (_available[(int)art][_dir] && ActionOn(Cfg(), art)) return art;
    if (art == Art::Walk || art == Art::Run || art == Art::Run2) {
        for (Art moving : {Art::Run2, Art::Walk, Art::Run})
            if (_available[(int)moving][_dir] && ActionOn(Cfg(), moving)) return moving;
    }
    if (art == Art::Down && _available[(int)Art::Kneel2][_dir] && ActionOn(Cfg(), Art::Kneel2)) return Art::Kneel2;
    for (Art fallback : {ArtKind(Cfg(), art), Art::Standby, Art::Walk, Art::Run2, Art::Run, Art::Kneel2}) {
        if (_available[(int)fallback][_dir] && ActionOn(Cfg(), fallback)) return fallback;
    }
    // Even a configuration disabling all art must not leave stale pixels.
    for (int a = 0; a < ART_COUNT; ++a) if (_available[a][_dir]) return (Art)a;
    return art;
}

double Runner::BattleDistance(const Runner& other) const {
    const auto a = BodyCenter(), b = other.BodyCenter();
    return _opt.taskbar && other._opt.taskbar ? std::abs(a.x - b.x) : std::hypot(a.x - b.x, a.y - b.y);
}

const Runner* Runner::SelectTarget() const {
    if (!Targetable() || !s_peers) return nullptr;
    const Runner* best = nullptr;
    const Runner* current = nullptr;
    double distance = 1e300;
    int priority = 99;
    for (const auto* other : *s_peers) {
        if (!other || other == this || !other->Targetable() || !Opposed(_faction, other->_faction)) continue;
        const double d = BattleDistance(*other);
        if (d > Cfg().encFarLen.valueOr(ENC_FAR_LEN) * BodyLen()) continue;
        const int rank = TargetPriority(_faction, other->_faction);
        if (other->_entityId == _targetId) current = other;
        if (rank < priority || (rank == priority && d < distance)) {
            best = other; priority = rank; distance = d;
        }
    }
    if (current && TargetPriority(_faction, current->_faction) == priority && BattleDistance(*current) <= distance * 1.2) return current;
    return best;
}

void Runner::BattleArt(Art art, bool once, bool restart) {
    const bool changed = !_battleActive || _battleArt != art || restart;
    _battleActive = true; _battleArt = art; _battleOnce = once; _hold = false;
    if (changed) { _animClock = 0; _frameIndex = -1; }
    LoadFrames();
    _needUpload |= changed;
}

void Runner::ResetBattle() {
    if (!_battleActive) return;
    _battleActive = false; _battleOnce = false; _targetId = 0;
    _returnTo = State::Walk; _stateClock = 0; _stateCool = 0;
    _state = State::Walk; _stats.state = (int)_state;
    _artSet = false; _hold = false; _animClock = 0; _frameIndex = -1;
    LoadFrames(); _needUpload = true;
}

void Runner::BattleMove(double dx, double dy) {
    _pos.x += dx; _pos.y += _opt.taskbar ? 0 : dy;
    if (_opt.taskbar) { _pos.x = (std::clamp)(_pos.x, _tbLoX, _tbHiX); _pos.y = _tbFootY; }
    else ClampPos();
}

void Runner::EnterFromEdge(int side) {
    if (!IsEnemy() || Downed()) return;
    _targetId = 0; _actionLeft = 0; _knockLeft = 0;
    if (_opt.taskbar) side &= 1;
    const double off = BodyLen() * 1.2;
    _enterEnd = _pos;
    if (_opt.taskbar) _enterEnd.y = _pos.y = _tbFootY;
    switch (side) {
    case 1:
        _pos.x = _ox + _worldW + (std::max)(off, ExtLeft() + 1);
        _enterEnd.x = _opt.taskbar ? _tbHiX : _ox + _worldW - PadRight() - BodyLen(); break;
    case 2:
        _pos.y = _oy - (std::max)(off, ExtDown() + 1);
        _enterEnd.y = _oy + PadTop() + BodyLen(); break;
    case 3:
        _pos.y = _oy + _worldH + (std::max)(off, ExtUp() + 1);
        _enterEnd.y = _oy + _worldH - PadBottom() - BodyLen(); break;
    default:
        _pos.x = _ox - (std::max)(off, ExtRight() + 1);
        _enterEnd.x = _opt.taskbar ? _tbLoX : _ox + PadLeft() + BodyLen(); break;
    }
    const Vec2 from = _pos;
    _pos = _enterEnd;
    if (!_opt.taskbar) ClampPos();
    _enterEnd = _pos; _pos = from;
    const double dx = _enterEnd.x - _pos.x, dy = _enterEnd.y - _pos.y;
    _enterLeft = std::hypot(dx, dy);
    _enterUnit = _enterLeft > 0 ? Vec2{dx / _enterLeft, dy / _enterLeft} : Vec2{};
    SetDirection(DirToward(dx, dy));
    BattleArt(Art::Run2);
}

bool Runner::TickBattle(double dt) {
    _battleCool = (std::max)(0.0, _battleCool - dt);
    _manualWeaponHold = (std::max)(0.0, _manualWeaponHold - dt);
    // Knockback belongs to the hit, independently of hurt animations and HP.
    // Process it even on a lethal hit or when react kind="none" is configured.
    const bool knocked = _knockLeft > 0;
    if (knocked) {
        const double step = (std::min)(_knockLeft, BodyLen() * dt / 0.35);
        BattleMove(_knockUnit.x * step, _knockUnit.y * step);
        _knockLeft = (std::max)(0.0, _knockLeft - step);
    }
    if (Downed()) {
        _lifeAge += dt;
        _targetId = 0;
        BattleArt(Art::Kneel2, true);
        if (_faction == Faction::Ally && _lifeAge >= 5.0) {
            _hp = MaxHp(); _lifeAge = 0; _battleCool = 0; _actionLeft = 0;
            ResetBattle();
            // Stand up without advancing the previous fall animation.
            BattleArt(Art::Standby); _actionLeft = 0.05;
        } else if (IsEnemy()) {
            const BYTE alpha = (BYTE)std::lround(255 * (std::clamp)(3.0 - _lifeAge, 0.0, 1.0));
            if (alpha != _opacity) { _opacity = alpha; _win->SetOpacity(alpha); _needUpload = true; }
        }
        return true;
    }
    if (Entering()) {
        const double step = (std::min)(_enterLeft, Cfg().walkPx.valueOr(WALK_PX) * _k * 0.5 * dt);
        _pos.x += _enterUnit.x * step; _pos.y += _enterUnit.y * step;
        _enterLeft -= step;
        if (_enterLeft <= 0.001) { _enterLeft = 0; _pos = _enterEnd; }
        BattleArt(Art::Run2); return true;
    }
    if (_actionLeft > 0) {
        _actionLeft -= dt;
        if (_actionLeft > 0) return true;
    }
    // Do not cancel the last recoil step by chasing forward on the same tick.
    if (knocked) return true;
    const auto* target = SelectTarget();
    if (!target) {
        _targetId = 0;
        _disarmClock += dt;
        if (_autoArmed && _disarmClock >= Cfg().disarmSec.valueOr(DISARM_SEC)) {
            SetArmed(false, true); _autoArmed = false;
        }
        if (!IsEnemy()) { ResetBattle(); return false; }
        _idleAge += dt;
        const double idle = Cfg().idleSec.valueOr(IDLE_SEC);
        if (_idleAge >= idle + 2) { _idleAge = 0; }
        if (_idleAge <= idle) BattleArt(Art::Standby);
        else {
            if (_idleAge - dt <= idle) SetDirection(std::uniform_int_distribution<int>(0, _opt.taskbar ? 1 : 7)(_rng) * (_opt.taskbar ? 4 : 1));
            const double step = Cfg().walkPx.valueOr(WALK_PX) * _k * 0.5 * dt;
            const auto before = _pos;
            BattleMove(DirDX()[_dir] * step, DirDY()[_dir] * step);
            if (_pos.x == before.x && _pos.y == before.y) SetDirection((_dir + 4) % 8);
            BattleArt(Art::Run2);
        }
        _state = State::Idle; _stats.state = (int)_state;
        return true;
    }
    _targetId = target->_entityId;
    _disarmClock = 0;
    if (!_armed && _manualWeaponHold <= 0 && _opt.hasWeaponArt) { _autoArmed = true; SetArmed(true, true); }
    const auto me = BodyCenter(), other = target->BodyCenter();
    const double dx = other.x - me.x, dy = _opt.taskbar ? 0 : other.y - me.y;
    SetDirection(DirToward(dx, dy));
    const double distance = BattleDistance(*target);
    const double reach = (std::max)(Cfg().attackLen.valueOr(ATTACK_LEN) * BodyLen(), HalfW() + target->HalfW() + PEER_PAD * _dpi + 2);
    // A chase can land a few floating-point ulps outside reach. The remaining
    // step then rounds back to the same position forever. Treat subpixel
    // contact as in range, at the same logical tolerance for every DPI.
    const double contactTolerance = 0.5 * _dpi;
    if (distance <= reach + contactTolerance) {
        _state = State::Idle;
        if (_battleCool <= 0) {
            _battleCool = (std::max)(0.05, Cfg().attackCoolSec.valueOr(ATTACK_COOL_SEC));
            BattleArt(Art::Attack, true, true);
            _state = State::Attack;
            _actionLeft = (std::max)(0.15, _frameCount / AnimationRate(Art::Attack));
            ++_attacks;
            const_cast<Runner*>(target)->Hit(this);
        } else BattleArt(Art::Standby);
    } else {
        _state = State::Chase;
        BattleArt(_armed || IsEnemy() ? Art::Run2 : Art::Run);
        const double norm = std::hypot(dx, dy);
        const double speed = Cfg().armedRunPx.valueOr(ARMED_RUN_PX) * _k * 0.5;
        const double step = (std::min)(distance - reach, speed * dt);
        if (norm > 0) BattleMove(dx / norm * step, dy / norm * step);
    }
    _stats.state = (int)_state;
    return true;
}
} // namespace fd
