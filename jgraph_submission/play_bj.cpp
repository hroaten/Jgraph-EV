
/*
This driver for gampelay was written with AI for the sake of allowing gameplay to update data used by Jgraphs,
future iterations will be hand-made
*/
// play_bj.cpp — minimal interactive blackjack driver using EV_Calculator.
// Builds against bj.h / bj.cpp with all members assumed public.
//
// Build: g++ -std=c++17 -O2 play_bj.cpp bj.cpp -o play_bj
// (add -fopenmp if bj.cpp was compiled with it; not required for play)
//
// Requires ./EV/<ruleset>_base.bj to exist (run base-EV generation first).

#include "bj.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <cmath>

// ---- Glyphs / pretty printing ----------------------------------------------

static const char RANK_GLYPH[RANKS] = {'2','3','4','5','6','7','8','9','T','A'};
static const char* MOVE_NAMES[4] = {"Stand", "Hit", "Double", "Split"};
static const char  MOVE_KEYS[4]  = {'s', 'h', 'd', 'p'};

static std::string render_hand(const std::vector<int>& cards) {
    std::string s;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (i) s += ' ';
        s += RANK_GLYPH[cards[i]];
    }
    return s;
}

static std::string total_str(const Hand& h) {
    char buf[32];
    if (h.soft) snprintf(buf, sizeof(buf), "soft %d", h.total);
    else        snprintf(buf, sizeof(buf), "%d",      h.total);
    return buf;
}

// ---- Random card draw from the live shoe -----------------------------------

static int draw_from_shoe(ShoeComp& SC, std::mt19937& rng) {
    return SC.random_draw(rng); // already removes the card from the shoe
}

// ---- EV lookup with the dealer up-card properly removed --------------------
// Workaround for the bug in calculate_ev where dealer_probs is called without
// first drawing dealer_up. Remove it for the duration of the lookup.

static std::array<double, 4>
ev_for(EV_Calculator& EVC, Hand player, int dealer_up, ShoeComp& SC) {
    SC.draw(dealer_up);
    auto evs = EVC.lookup_ev(player, dealer_up, SC);
    SC.undraw(dealer_up);
    return evs;
}

// ---- Print EVs and the recommended move ------------------------------------

static int print_evs_and_recommend(const std::array<double,4>& evs) {
    int best = -1;
    double best_ev = -std::numeric_limits<double>::infinity();
    for (int m = 0; m < 4; ++m) {
        if (std::isfinite(evs[m]) && evs[m] > best_ev) {
            best_ev = evs[m];
            best = m;
        }
    }
    std::printf("  EVs:");
    for (int m = 0; m < 4; ++m) {
        if (!std::isfinite(evs[m])) {
            std::printf("  %-6s --     ", MOVE_NAMES[m]);
        } else {
            std::printf("  %-6s %+6.2f%%%s",
                        MOVE_NAMES[m], evs[m] * 100.0,
                        (m == best ? "*" : " "));
        }
    }
    std::printf("\n");
    return best;
}

// ---- Dealer playout ---------------------------------------------------------
// Plays the dealer to completion using rules.H17 (hit on soft 17 if 1).
// Cards drawn here are also removed from the shoe.

static void dealer_play(Hand& dealer, std::vector<int>& dealer_cards,
                        ShoeComp& SC, std::mt19937& rng,
                        const ruleset& rules) {
    // Standard rules: stand on hard 17+; on soft 17, hit if H17.
    while (true) {
        if (dealer.total > 21) return;            // bust
        if (dealer.total >= 18) return;           // always stand
        if (dealer.total == 17) {
            if (dealer.soft && rules.H17) {
                // hit
            } else {
                return;
            }
        }
        if (dealer.total < 17 || (dealer.total == 17 && dealer.soft && rules.H17)) {
            int r = draw_from_shoe(SC, rng);
            dealer_cards.push_back(r);
            dealer.add_card(r);
        }
    }
}

// ---- Settle one finished hand vs dealer -------------------------------------
// Returns net units (e.g. -1, 0, +1, +1.5 for natural).
// `bet_mult` is 2 for doubled hands, 1 otherwise. `was_natural` only applies
// to the original 2-card hand (split hands cannot make a "natural").

static double settle(const Hand& player, const Hand& dealer,
                      double bet_mult, bool was_natural) {
    if (player.total > 21) return -1.0 * bet_mult;
    if (was_natural && dealer.total != 21) return +1.5;       // 3:2 on natural
    if (was_natural && dealer.total == 21 && dealer.n_cards == 2) return 0.0;
    if (dealer.total > 21) return +1.0 * bet_mult;
    if (player.total > dealer.total) return +1.0 * bet_mult;
    if (player.total < dealer.total) return -1.0 * bet_mult;
    return 0.0;                                                // push
}

// ---- Read one move character from stdin ------------------------------------
// Filters to the legal moves for this hand (which depend on can_double / pair).

static char prompt_move(const Hand& h, const ruleset& rules,
                         bool allow_split) {
    std::string legal = "sh";
    if (h.can_double) legal += 'd';
    if (allow_split && h.pair >= 0) legal += 'p';

    while (true) {
        std::printf("  Move [%s]: ", legal.c_str());
        std::fflush(stdout);
        std::string line;
        if (!std::getline(std::cin, line)) {
            std::printf("\n(eof — quitting)\n");
            std::exit(0);
        }
        if (line.empty()) continue;
        char c = (char)std::tolower((unsigned char)line[0]);
        if (c == 'q') { std::printf("Goodbye.\n"); std::exit(0); }
        if (legal.find(c) != std::string::npos) return c;
        std::printf("  (invalid; try one of '%s' or 'q' to quit)\n", legal.c_str());
    }
}

// ---- Play one player hand to completion ------------------------------------
// Returns the final Hand and a flag for whether double was taken.
// Splits are handled at a higher level; this function never recurses on split.

struct HandResult {
    Hand final;
    std::vector<int> cards;
    double bet_mult;     // 1 normally, 2 if doubled
};

static HandResult play_one_hand(Hand start, std::vector<int> cards,
                                 int dealer_up,
                                 ShoeComp& SC, std::mt19937& rng,
                                 EV_Calculator& EVC,
                                 const ruleset& rules,
                                 bool from_split) {
    Hand h = start;
    double bet_mult = 1.0;

    while (true) {
        // Bust → done.
        if (h.total > 21) {
            std::printf("  Player [%s] = %d -> BUST\n",
                        render_hand(cards).c_str(), h.total);
            break;
        }
        // 21 → auto-stand (no need to ask).
        if (h.total == 21) {
            std::printf("  Player [%s] = 21 -> stand\n",
                        render_hand(cards).c_str());
            break;
        }

        std::printf("  Player [%s] = %s\n",
                    render_hand(cards).c_str(),
                    total_str(h).c_str());

        // Show EVs. Note: ev_for already adjusts for the up-card.
        auto evs = ev_for(EVC, h, dealer_up, SC);
        // Suppress SPLIT in display if we can't actually split here.
        if (from_split || h.pair < 0) evs[SPLIT] = -std::numeric_limits<double>::infinity();
        if (!h.can_double)            evs[DOUBLE] = -std::numeric_limits<double>::infinity();
        print_evs_and_recommend(evs);

        char move = prompt_move(h, rules, /*allow_split=*/!from_split);

        if (move == 's') break;
        if (move == 'h') {
            int r = draw_from_shoe(SC, rng);
            cards.push_back(r);
            h.add_card(r);
            continue;
        }
        if (move == 'd') {
            int r = draw_from_shoe(SC, rng);
            cards.push_back(r);
            h.add_card(r);
            bet_mult = 2.0;
            std::printf("  Doubled. Player [%s] = %s\n",
                        render_hand(cards).c_str(),
                        total_str(h).c_str());
            break;
        }
        if (move == 'p') {
            // Caller handles split; signal by returning with a special marker.
            // We use bet_mult = -1 to mean "split requested".
            HandResult r;
            r.final = h;
            r.cards = cards;
            r.bet_mult = -1.0;
            return r;
        }
    }

    HandResult res;
    res.final = h;
    res.cards = cards;
    res.bet_mult = bet_mult;
    return res;
}

// ---- One full round --------------------------------------------------------

static double play_round(ShoeComp& SC, std::mt19937& rng,
                          EV_Calculator& EVC, const ruleset& rules) {
    // Reshuffle if past the cut.
    int cut_off = (int)(rules.cut_depth * 52);
    if (SC.cards_left <= cut_off) {
        std::printf("\n*** Reshuffling the shoe ***\n");
        SC.shuffle();
    }

    std::printf("\n----- New hand -----\n");
    std::printf("Cards left: %d   RC: %+d   TC: %+.2f\n",
                SC.cards_left, SC.running_count(), SC.true_count());

    // Deal: P1, D1 (up), P2, D2 (hole).
    int p1 = draw_from_shoe(SC, rng);
    int du = draw_from_shoe(SC, rng);
    int p2 = draw_from_shoe(SC, rng);
    int dh = draw_from_shoe(SC, rng);

    Hand player; player.add_card(p1); player.add_card(p2);
    Hand dealer; dealer.add_card(du); dealer.add_card(dh);
    std::vector<int> player_cards = {p1, p2};
    std::vector<int> dealer_cards = {du, dh};

    std::printf("Dealer shows: %c\n", RANK_GLYPH[du]);
    std::printf("Player [%s] = %s\n",
                render_hand(player_cards).c_str(),
                total_str(player).c_str());

    bool player_natural = (player.total == 21);
    bool dealer_natural = (dealer.total == 21);

    // Dealer peeks on Ace/Ten up (no insurance offered per rules.Ins == 0).
    if ((du == R_A || du == R_T) && dealer_natural) {
        std::printf("Dealer has blackjack. Dealer hand: [%s]\n",
                    render_hand(dealer_cards).c_str());
        if (player_natural) { std::printf("Push.\n"); return 0.0; }
        std::printf("Player loses 1 unit.\n");
        return -1.0;
    }
    if (player_natural) {
        std::printf("Player blackjack! Pays 3:2.\n");
        std::printf("Dealer hand at showdown: [%s] = %s\n",
                    render_hand(dealer_cards).c_str(),
                    total_str(dealer).c_str());
        return +1.5;
    }

    // Play the player's hand(s). We support one level of split (no re-splits
    // for simplicity in this driver).
    std::vector<HandResult> finished;
    HandResult first = play_one_hand(player, player_cards, du,
                                      SC, rng, EVC, rules, /*from_split=*/false);

    if (first.bet_mult == -1.0) {
        // Split requested.
        int pair_rank = first.final.pair;
        std::printf("  *** Splitting %c%c ***\n",
                    RANK_GLYPH[pair_rank], RANK_GLYPH[pair_rank]);

        for (int hand_idx = 0; hand_idx < 2; ++hand_idx) {
            std::printf("  -- Split hand %d --\n", hand_idx + 1);
            int extra = draw_from_shoe(SC, rng);
            Hand sh; sh.add_card(pair_rank); sh.add_card(extra);
            sh.split_hands = 2;
            std::vector<int> scards = {pair_rank, extra};

            // Special case: split aces get one card and stop.
            if (pair_rank == R_A) {
                std::printf("  Player [%s] = %s (aces get one card, stand)\n",
                            render_hand(scards).c_str(),
                            total_str(sh).c_str());
                HandResult r; r.final = sh; r.cards = scards; r.bet_mult = 1.0;
                finished.push_back(r);
                continue;
            }

            HandResult sub = play_one_hand(sh, scards, du,
                                            SC, rng, EVC, rules,
                                            /*from_split=*/true);
            // Disallow re-split in this driver
            if (sub.bet_mult == -1.0) {
                std::printf("  (re-splits not supported by driver; treating as stand)\n");
                sub.bet_mult = 1.0;
            }
            finished.push_back(sub);
        }
    } else {
        finished.push_back(first);
    }

    // If every player hand busted, dealer doesn't draw.
    bool any_alive = false;
    for (const auto& r : finished) if (r.final.total <= 21) { any_alive = true; break; }
    if (any_alive) {
        dealer_play(dealer, dealer_cards, SC, rng, rules);
    }
    std::printf("Dealer hand at showdown: [%s] = %s%s\n",
                render_hand(dealer_cards).c_str(),
                total_str(dealer).c_str(),
                dealer.total > 21 ? " (BUST)" : "");

    double net = 0.0;
    for (size_t i = 0; i < finished.size(); ++i) {
        const HandResult& r = finished[i];
        // Splits cannot produce a "natural" payout.
        bool nat = (finished.size() == 1) && false; // handled earlier; never here
        double delta = settle(r.final, dealer, r.bet_mult, nat);
        if (finished.size() > 1) std::printf("  Hand %zu: ", i + 1);
        std::printf("  Result: %+.2f\n", delta);
        net += delta;
    }
    return net;
}

// ---- main ------------------------------------------------------------------

int main(int argc, char** argv) {
    ruleset rules;
    if (argc >= 2) rules.name = argv[1];

    EV_Calculator EVC(rules);
    EVC.EVT.load();   // load ./EV/<rules.name>(_base).bj

    ShoeComp SC(rules.decks);
    std::mt19937 rng{std::random_device{}()};

    std::printf("Blackjack proof-of-concept driver\n");
    std::printf("Rules: %s, %d decks, H17=%d, DAS=%d, max_splits=%d\n",
                rules.name.c_str(), rules.decks, rules.H17, rules.DAS, rules.max_splits);
    std::printf("Moves: s=stand, h=hit, d=double, p=split, q=quit\n");

    double bankroll = 0.0;
    int hands = 0;
    while (true) {
        double delta = play_round(SC, rng, EVC, rules);
        bankroll += delta;
        hands++;
        std::printf("Bankroll: %+.2f units over %d hand%s\n",
                    bankroll, hands, hands == 1 ? "" : "s");
    }
    return 0;
}