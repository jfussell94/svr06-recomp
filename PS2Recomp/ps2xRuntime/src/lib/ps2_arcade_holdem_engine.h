// SvR06 PSP Arcade: WWE Texas Hold'em - game rules (no rendering, no raylib). CL-0289.
// No-limit Texas Hold'em, one human (seat 0) against computer players. Side pots, all-ins, heads-up blind rule,
// rising blinds. Win by bankrupting every opponent (the PSP mode's goal).
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace svr06holdem
{
inline int rankOf(int c) { return c % 13; }   // 0 = 2 ... 12 = Ace
inline int suitOf(int c) { return c / 13; }   // 0 clubs, 1 diamonds, 2 hearts, 3 spades

// ---- hand value: category << 20 | five 4-bit kickers (higher is better)
inline int topBits(uint32_t mask, int n, int *out)
{
    int k = 0;
    for (int r = 12; r >= 0 && k < n; --r) if (mask & (1u << r)) out[k++] = r;
    return k;
}
inline int straightHigh(uint32_t m)
{
    for (int hi = 12; hi >= 4; --hi) if (((m >> (hi - 4)) & 0x1Fu) == 0x1Fu) return hi;
    if ((m & 0x100Fu) == 0x100Fu) return 3;   // A-2-3-4-5
    return -1;
}
inline uint32_t pack(int cat, const int *k, int n)
{
    uint32_t v = (uint32_t)cat << 20;
    for (int i = 0; i < 5; ++i) v |= (uint32_t)(i < n ? k[i] : 0) << (16 - 4 * i);
    return v;
}
inline uint32_t evalCards(const int *c, int n)
{
    int cnt[13] = {0}; uint32_t suitMask[4] = {0, 0, 0, 0}, all = 0;
    for (int i = 0; i < n; ++i) { cnt[rankOf(c[i])]++; suitMask[suitOf(c[i])] |= 1u << rankOf(c[i]); all |= 1u << rankOf(c[i]); }
    int k[5];
    for (int s = 0; s < 4; ++s)
        if (__builtin_popcount(suitMask[s]) >= 5)
        {
            const int sh = straightHigh(suitMask[s]);
            if (sh >= 0) { k[0] = sh; return pack(8, k, 1); }
            topBits(suitMask[s], 5, k); return pack(5, k, 5);
        }
    int quad = -1, trips[2] = {-1, -1}, pairs[3] = {-1, -1, -1}, nt = 0, np = 0;
    for (int r = 12; r >= 0; --r)
    {
        if (cnt[r] == 4 && quad < 0) quad = r;
        else if (cnt[r] == 3 && nt < 2) trips[nt++] = r;
        else if (cnt[r] == 2 && np < 3) pairs[np++] = r;
    }
    if (quad >= 0) { k[0] = quad; topBits(all & ~(1u << quad), 1, k + 1); return pack(7, k, 2); }
    if (nt >= 1 && (nt >= 2 || np >= 1))
    {
        k[0] = trips[0]; k[1] = nt >= 2 ? std::max(trips[1], pairs[0]) : pairs[0];
        return pack(6, k, 2);
    }
    const int sh = straightHigh(all);
    if (sh >= 0) { k[0] = sh; return pack(4, k, 1); }
    if (nt == 1) { k[0] = trips[0]; topBits(all & ~(1u << trips[0]), 2, k + 1); return pack(3, k, 3); }
    if (np >= 2) { k[0] = pairs[0]; k[1] = pairs[1]; topBits(all & ~(1u << pairs[0]) & ~(1u << pairs[1]), 1, k + 2); return pack(2, k, 3); }
    if (np == 1) { k[0] = pairs[0]; topBits(all & ~(1u << pairs[0]), 3, k + 1); return pack(1, k, 4); }
    topBits(all, 5, k); return pack(0, k, 5);
}

inline const char *rankName(int r, bool plural)
{
    static const char *s[] = {"Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine", "Ten", "Jack", "Queen", "King", "Ace"};
    static const char *p[] = {"Twos", "Threes", "Fours", "Fives", "Sixes", "Sevens", "Eights", "Nines", "Tens", "Jacks", "Queens", "Kings", "Aces"};
    return (r >= 0 && r < 13) ? (plural ? p[r] : s[r]) : "?";
}
inline std::string handName(uint32_t v)
{
    const int cat = (int)(v >> 20), a = (v >> 16) & 15, b = (v >> 12) & 15;
    switch (cat)
    {
    case 8: return a == 12 ? "a royal flush" : std::string("a straight flush, ") + rankName(a, false) + " high";
    case 7: return std::string("four ") + rankName(a, true);
    case 6: return std::string("a full house, ") + rankName(a, true) + " over " + rankName(b, true);
    case 5: return std::string("a flush, ") + rankName(a, false) + " high";
    case 4: return std::string("a straight, ") + rankName(a, false) + " high";
    case 3: return std::string("three ") + rankName(a, true);
    case 2: return std::string("two pair, ") + rankName(a, true) + " and " + rankName(b, true);
    case 1: return std::string("a pair of ") + rankName(a, true);
    default: return std::string(rankName(a, false)) + " high";
    }
}

enum class Street { Preflop, Flop, Turn, River, Showdown };
enum class Act { None, Fold, Check, Call, Bet, Raise, AllIn };

struct Player
{
    std::string name;
    bool human = false;
    int chips = 0;
    int bet = 0;       // this street
    int total = 0;     // this hand
    int cards[2] = {-1, -1};
    bool folded = false, allin = false, acted = false, out = false;
    Act last = Act::None; int lastAmt = 0;
    float aggression = 1.0f, looseness = 1.0f;
};

struct Win { int seat; int amount; uint32_t value; };

struct Table
{
    std::vector<Player> p;
    int board[5] = {-1, -1, -1, -1, -1};
    int nBoard = 0;
    std::array<int, 52> deck{}; int deckPos = 0;
    int dealer = -1, toAct = -1;
    int sb = 50, bb = 100, currentBet = 0, minRaise = 100, hand = 0;
    Street street = Street::Preflop;
    bool handOver = true, showdown = false;
    std::vector<Win> wins;
    std::mt19937 rng{std::random_device{}()};

    void init(const std::vector<std::string> &names, int startChips)
    {
        p.clear();
        for (size_t i = 0; i < names.size(); ++i)
        {
            Player pl; pl.name = names[i]; pl.human = i == 0; pl.chips = startChips;
            std::uniform_real_distribution<float> d(0.75f, 1.35f);
            pl.aggression = d(rng); pl.looseness = d(rng);
            p.push_back(pl);
        }
        dealer = -1; hand = 0; sb = 50; bb = 100; handOver = true;
    }
    int alive() const { int n = 0; for (auto &x : p) n += !x.out; return n; }
    int inHand() const { int n = 0; for (auto &x : p) n += !x.out && !x.folded; return n; }
    int canAct() const { int n = 0; for (auto &x : p) n += !x.out && !x.folded && !x.allin; return n; }
    int pot() const { int s = 0; for (auto &x : p) s += x.total; return s; }
    int next(int i, bool needCanAct) const
    {
        const int n = (int)p.size();
        for (int k = 1; k <= n; ++k)
        {
            const int j = (i + k) % n; const Player &x = p[j];
            if (x.out || x.folded) continue;
            if (needCanAct && x.allin) continue;
            return j;
        }
        return -1;
    }
    int nextAlive(int i) const { const int n = (int)p.size(); for (int k = 1; k <= n; ++k) if (!p[(i + k) % n].out) return (i + k) % n; return -1; }
    bool gameOver() const { return p[0].out || alive() <= 1; }

    void post(int s, int amt, Act a)
    {
        Player &x = p[s]; const int v = std::min(amt, x.chips);
        x.chips -= v; x.bet += v; x.total += v; x.last = a; x.lastAmt = x.bet;
        if (x.chips == 0) x.allin = true;
    }
    void startHand()
    {
        ++hand;
        if (hand > 1 && (hand - 1) % 8 == 0) { sb = sb * 3 / 2 / 25 * 25; if (sb < 50) sb = 50; bb = sb * 2; }   // blinds rise every 8 hands
        for (auto &x : p) { x.bet = x.total = 0; x.folded = x.out; x.allin = false; x.acted = false; x.cards[0] = x.cards[1] = -1; x.last = Act::None; x.lastAmt = 0; }
        for (int i = 0; i < 52; ++i) deck[i] = i;
        std::shuffle(deck.begin(), deck.end(), rng); deckPos = 0;
        nBoard = 0; for (int &b : board) b = -1;
        wins.clear(); showdown = false; handOver = false; street = Street::Preflop;
        dealer = nextAlive(dealer < 0 ? (int)p.size() - 1 : dealer);
        int sbSeat, bbSeat;
        if (alive() == 2) { sbSeat = dealer; bbSeat = nextAlive(dealer); }
        else { sbSeat = nextAlive(dealer); bbSeat = nextAlive(sbSeat); }
        for (int r = 0; r < 2; ++r) for (int k = 1; k <= (int)p.size(); ++k) { Player &x = p[(dealer + k) % p.size()]; if (!x.out) x.cards[r] = deck[deckPos++]; }
        post(sbSeat, sb, Act::Bet); post(bbSeat, bb, Act::Bet);
        p[sbSeat].last = Act::None; p[bbSeat].last = Act::None;
        currentBet = bb; minRaise = bb;
        toAct = next(bbSeat, true);
        if (canAct() == 0 || (canAct() == 1 && p[toAct >= 0 ? toAct : 0].bet >= currentBet)) toAct = -1;
        if (toAct < 0) runOut();
    }
    int toCall(int s) const { return std::max(0, std::min(currentBet - p[s].bet, p[s].chips)); }
    int minRaiseTo(int s) const { return std::min(currentBet + minRaise, p[s].bet + p[s].chips); }
    int maxRaiseTo(int s) const { return p[s].bet + p[s].chips; }

    // apply an action for the player to act; amount = raise-to total for Bet/Raise
    void act(Act a, int raiseTo = 0)
    {
        if (toAct < 0 || handOver) return;
        Player &x = p[toAct];
        switch (a)
        {
        case Act::Fold: x.folded = true; x.last = Act::Fold; break;
        case Act::Check: if (x.bet < currentBet) { act(Act::Call); return; } x.last = Act::Check; break;
        case Act::Call: { const int c = toCall(toAct); post(toAct, c, Act::Call); if (c == 0) x.last = Act::Check; if (x.allin) x.last = Act::AllIn; break; }
        case Act::Bet: case Act::Raise: case Act::AllIn:
        {
            int to = a == Act::AllIn ? maxRaiseTo(toAct) : std::max(raiseTo, minRaiseTo(toAct));
            to = std::min(to, maxRaiseTo(toAct));
            if (to <= currentBet) { act(Act::Call); return; }
            const int raiseBy = to - currentBet;
            if (raiseBy >= minRaise) { minRaise = raiseBy; for (auto &o : p) o.acted = false; }   // full raise reopens the betting
            currentBet = to;
            post(toAct, to - x.bet, currentBet == 0 ? Act::Bet : (a == Act::AllIn ? Act::AllIn : a));
            x.last = x.allin ? Act::AllIn : (raiseBy == to ? Act::Bet : Act::Raise);
            break;
        }
        default: return;
        }
        x.acted = true;
        advance();
    }
    bool roundDone() const
    {
        for (auto &x : p)
        {
            if (x.out || x.folded || x.allin) continue;
            if (!x.acted || x.bet < currentBet) return false;
        }
        return true;
    }
    void advance()
    {
        if (inHand() == 1) { finishFold(); return; }
        if (!roundDone()) { toAct = next(toAct, true); if (toAct >= 0) return; }
        // street over
        if (canAct() <= 1)
        {
            // nobody (or one player) can still bet: if the one remaining actor still owes a call it was handled above; deal out
            bool owe = false;
            for (int i = 0; i < (int)p.size(); ++i) { const Player &x = p[i]; if (!x.out && !x.folded && !x.allin && x.bet < currentBet) owe = true; }
            if (!owe) { runOut(); return; }
        }
        nextStreet();
    }
    void dealBoard(int n) { for (int i = 0; i < n && nBoard < 5; ++i) { ++deckPos; board[nBoard++] = deck[deckPos++]; } }   // burn + deal
    void nextStreet()
    {
        for (auto &x : p) { x.bet = 0; x.acted = false; if (!x.folded && !x.allin) x.last = Act::None; }
        currentBet = 0; minRaise = bb;
        if (street == Street::Preflop) { street = Street::Flop; dealBoard(3); }
        else if (street == Street::Flop) { street = Street::Turn; dealBoard(1); }
        else if (street == Street::Turn) { street = Street::River; dealBoard(1); }
        else { doShowdown(); return; }
        toAct = next(dealer, true);
        if (canAct() <= 1) { runOut(); return; }
    }
    void runOut()
    {
        for (auto &x : p) x.bet = 0;
        if (nBoard < 3) dealBoard(3 - nBoard);
        while (nBoard < 5) dealBoard(1);
        doShowdown();
    }
    void finishFold()
    {
        int w = -1; for (int i = 0; i < (int)p.size(); ++i) if (!p[i].out && !p[i].folded) w = i;
        const int amt = pot();
        p[w].chips += amt; wins.push_back({w, amt, 0});
        endHand(false);
    }
    uint32_t value(int s) const
    {
        int c[7]; int n = 0; c[n++] = p[s].cards[0]; c[n++] = p[s].cards[1];
        for (int i = 0; i < nBoard; ++i) c[n++] = board[i];
        return evalCards(c, n);
    }
    void doShowdown()
    {
        toAct = -1; street = Street::Showdown;
        std::vector<int> levels;
        for (auto &x : p) if (x.total > 0) levels.push_back(x.total);
        std::sort(levels.begin(), levels.end()); levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
        int prev = 0; int carry = 0;
        std::vector<uint32_t> val(p.size(), 0);
        for (int i = 0; i < (int)p.size(); ++i) if (!p[i].out && !p[i].folded) val[i] = value(i);
        std::vector<int> lastElig;
        for (int lv : levels)
        {
            int slice = carry; carry = 0;
            for (auto &x : p) slice += std::max(0, std::min(x.total, lv) - std::min(x.total, prev));
            std::vector<int> elig;
            for (int i = 0; i < (int)p.size(); ++i) if (!p[i].out && !p[i].folded && p[i].total >= lv) elig.push_back(i);
            prev = lv;
            if (elig.empty()) { // only folded money above every live player's stake: goes back to the live players' last pot
                if (!lastElig.empty()) elig = lastElig; else { carry = slice; continue; }
            }
            lastElig = elig;
            uint32_t best = 0; for (int i : elig) best = std::max(best, val[i]);
            std::vector<int> ws; for (int i : elig) if (val[i] == best) ws.push_back(i);
            const int share = slice / (int)ws.size(); int odd = slice - share * (int)ws.size();
            // odd chips go to the first winner left of the dealer
            std::sort(ws.begin(), ws.end(), [&](int a, int b) { return (a - dealer + (int)p.size()) % (int)p.size() < (b - dealer + (int)p.size()) % (int)p.size(); });
            for (int i : ws)
            {
                const int amt = share + (odd > 0 ? 1 : 0); if (odd > 0) --odd;
                p[i].chips += amt;
                bool merged = false;
                for (auto &w : wins) if (w.seat == i) { w.amount += amt; merged = true; }
                if (!merged) wins.push_back({i, amt, val[i]});
            }
        }
        endHand(true);
    }
    void endHand(bool sd)
    {
        showdown = sd; handOver = true; toAct = -1;
        for (auto &x : p) if (!x.out && x.chips == 0) x.out = true;
    }
};

// ---- computer player: Monte Carlo equity against the live opponents, pot odds, personality
inline float equity(const Table &t, int seat, int iters, std::mt19937 &rng)
{
    int used[52] = {0};
    used[t.p[seat].cards[0]] = used[t.p[seat].cards[1]] = 1;
    for (int i = 0; i < t.nBoard; ++i) used[t.board[i]] = 1;
    std::vector<int> rest; for (int c = 0; c < 52; ++c) if (!used[c]) rest.push_back(c);
    int opp = 0; for (int i = 0; i < (int)t.p.size(); ++i) if (i != seat && !t.p[i].out && !t.p[i].folded) ++opp;
    if (opp == 0) return 1.0f;
    double score = 0;
    for (int it = 0; it < iters; ++it)
    {
        // partial shuffle: enough cards for the board and the opponents
        const int need = (5 - t.nBoard) + 2 * opp;
        for (int i = 0; i < need; ++i) { std::uniform_int_distribution<int> d(i, (int)rest.size() - 1); std::swap(rest[i], rest[d(rng)]); }
        int b[5]; int nb = 0; for (int i = 0; i < t.nBoard; ++i) b[nb++] = t.board[i];
        int k = 0; while (nb < 5) b[nb++] = rest[k++];
        int mine[7] = {t.p[seat].cards[0], t.p[seat].cards[1], b[0], b[1], b[2], b[3], b[4]};
        const uint32_t mv = evalCards(mine, 7);
        bool lose = false; int ties = 0;
        for (int o = 0; o < opp; ++o)
        {
            int oc[7] = {rest[k], rest[k + 1], b[0], b[1], b[2], b[3], b[4]}; k += 2;
            const uint32_t ov = evalCards(oc, 7);
            if (ov > mv) { lose = true; break; }
            if (ov == mv) ++ties;
        }
        if (!lose) score += ties ? 1.0 / (ties + 1) : 1.0;
    }
    return (float)(score / iters);
}

struct Decision { Act a; int to; };
inline Decision decide(const Table &t, int s, std::mt19937 &rng)
{
    const Player &x = t.p[s];
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const float eq = equity(t, s, t.nBoard == 0 ? 220 : 320, rng);
    const int call = t.toCall(s), pot = t.pot();
    const float odds = call > 0 ? (float)call / (float)(pot + call) : 0.0f;
    int opp = 0; for (int i = 0; i < (int)t.p.size(); ++i) if (i != s && !t.p[i].out && !t.p[i].folded) ++opp;
    const float fair = 1.0f / (opp + 1);                 // equity of an average hand
    const float strength = eq / std::max(0.05f, fair);  // >1 = better than average
    const float r = U(rng);
    auto raiseTo = [&](float potFrac) {
        int amt = t.currentBet + std::max(t.minRaise, (int)((pot + call) * potFrac));
        amt = (amt + 24) / 25 * 25;
        amt = std::max(amt, t.minRaiseTo(s)); amt = std::min(amt, t.maxRaiseTo(s));
        return amt;
    };
    if (call == 0)
    {
        if (strength > 1.9f / x.aggression || (strength > 1.35f && r < 0.45f * x.aggression)) return {Act::Bet, raiseTo(0.45f + 0.4f * U(rng))};
        if (r < 0.08f * x.aggression * x.looseness) return {Act::Bet, raiseTo(0.4f)};   // bluff
        return {Act::Check, 0};
    }
    const bool cheap = call <= t.bb && call * 12 < x.chips;
    if (eq < odds * 0.95f / x.looseness && !(cheap && r < 0.35f * x.looseness))
    {
        if (r < 0.04f * x.aggression) return {Act::Raise, raiseTo(0.8f)};   // rare re-bluff
        return {Act::Fold, 0};
    }
    if (strength > 2.3f / x.aggression && r < 0.7f) {
        if (strength > 3.0f && r < 0.25f) return {Act::AllIn, t.maxRaiseTo(s)};
        return {Act::Raise, raiseTo(0.6f + 0.5f * U(rng))};
    }
    if (call >= x.chips && eq < 0.5f * x.looseness && strength < 1.6f) return {Act::Fold, 0};
    return {Act::Call, 0};
}
} // namespace svr06holdem
