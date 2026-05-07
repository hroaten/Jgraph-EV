#ifdef _OPENMP
  #include <omp.h>
#endif
#include "bj.h"

/*
TODO:
Write Split EV Calculation
Write Insurance EV calculation
Write update state helper function:
    - update probabilities based on count
    - update player state to be used in EV calculation

Graphics Implementation
Binary File cache Implementation

WAYYYYYY later down the road 
    - ruleset variations

*/


/*
Shoe_Comp strcuture/class:
Cards/decks left,
Running count, 
Cutoff
*/


static constexpr string_view EV_DIR= "./EV";
static constexpr int BASE_TRIALS = 250;
static constexpr float REF_DEPTH = 0.5;

static constexpr uint32_t EV_MAGIC = 0x424A4556; // "BJEV"

struct EVHeader{ //add version and other information later (ruleset struct)
uint32_t magic;
uint32_t entry_size;
uint32_t total_entries;
};


ShoeComp::ShoeComp(int num_decks){//populate shoe based on # decks
    total_cards = 52 * num_decks;
    for(int i = 0; i < RANKS; i++) new_shoe[i] = i == R_T ? 16 * num_decks : 4 * num_decks;    
    shoe = new_shoe;
    cards_left = total_cards;
}

void ShoeComp::shuffle(){//reset shoe on shuffle
    shoe = new_shoe;
    cards_left = total_cards;
}


ev_buckets::ev_buckets(EVTable& EVT, Hand& hand, int dealer_up, double tc){
    const double tc_clamped = clamp(tc, TC_MIN, TC_MAX);
    const double bucket_real = (tc_clamped - TC_MIN) / TC_BUCKET_SIZE;

    lo = (int)floor(bucket_real);
    lo = clamp(lo, 0, NUM_TC_BUCKETS - 1);
    hi = min(lo + 1, NUM_TC_BUCKETS - 1);

    double frac = bucket_real - lo;
    w_lo = (lo == hi) ? 1.0 : 1.0 - frac;
    w_hi = (lo == hi) ? 0.0 : frac;
    w_lo = 1 - frac;
    HandCat hc = hand.categorize();

    lo_entry = &EVT.at(hc, dealer_up, lo);
    hi_entry = &EVT.at(hc, dealer_up, hi);
}

EVTable::EVTable(string rule_name){
    ev_file = (string)EV_DIR + "/" + rule_name + ".bj";
    base_file = (string)EV_DIR + "/" + rule_name + "_base.bj";
    if(!fs::exists(EV_DIR)) fs::create_directories(EV_DIR);
}

void EVTable::load(){
    string filepath = fs::exists(ev_file) ? ev_file : base_file; //loads from updated evs if the file exists
    FILE *f = fopen(filepath.c_str(), "rb");
    EVHeader header;
    if (fread(&header, sizeof(EVHeader), 1, f) != 1){
        printf("Failed to read header\n");
    }

    if(header.magic != EV_MAGIC ||
        header.entry_size != sizeof(ev_entry)
    ) {printf("Bad header\n");}

    entries.resize(header.total_entries);

    if(fread(entries.data(), sizeof(ev_entry), header.total_entries, f) != header.total_entries){
        printf("Failed to read %d entries from %s", header.total_entries, filepath.c_str());
    }

    fclose(f);
}

void EVTable::load_base(){
    string filepath = fs::exists(ev_file) ? ev_file : base_file; //loads from updated evs if the file exists
    FILE *f = fopen(filepath.c_str(), "rb");
    EVHeader header;
    if (fread(&header, sizeof(EVHeader), 1, f) != 1){
        printf("Failed to read header\n");
    }

    if(header.magic != EV_MAGIC ||
        header.entry_size != sizeof(ev_entry)
    ) {printf("Bad header\n");}

    entries.resize(header.total_entries);

    if(fread(entries.data(), sizeof(ev_entry), header.total_entries, f) != header.total_entries){
        printf("Failed to read %d entries from %s", header.total_entries, filepath.c_str());
    }

    fclose(f);
}
void EVTable::save(){
    string filepath = ev_file;
    EVHeader header{
        EV_MAGIC,
        sizeof(ev_entry),
        (uint32_t)entries.size()
    };

    FILE *f = fopen(filepath.c_str(), "wb");
    if(fwrite(&header, sizeof(EVHeader), 1, f) != 1)printf("Failed to write header\n");
    if(fwrite(entries.data(), sizeof(ev_entry), entries.size(), f) != entries.size()) printf("Failed to write EV-Table\n");

    fclose(f);
}

void EVTable::save_base(){
    string filepath = base_file;
    EVHeader header{
        EV_MAGIC,
        sizeof(ev_entry),
        (uint32_t)entries.size()
    };

    FILE *f = fopen(filepath.c_str(), "wb");
    if(fwrite(&header, sizeof(EVHeader), 1, f) != 1)printf("Failed to write header\n");
    if(fwrite(entries.data(), sizeof(ev_entry), entries.size(), f) != entries.size()) printf("Failed to write base EV-Table\n");
    
    if(!fs::exists(ev_file)) save(); //save base to cumulative file if it doesn't exist yet
    fclose(f);
}

void EVTable::update(array<double, 4>& evs, Hand& hand, int dealer_up, double tc){
    ev_buckets buckets(*this, hand, dealer_up, tc);
    
    auto accumulate = [&](ev_entry* entry, double w) {   // <-- defined here
        for (auto move : MOVES) {
            double ev = evs[move];
            if (ev != NONE) {
                #pragma omp atomic
                entry->ev_sum[move]    += ev * w;
                #pragma omp atomic
                entry->ev_weight[move] += w;
                #pragma omp atomic
                entry->ev_sq_sum[move] += (ev * ev) * w;
            }
        }
    };

    accumulate(buckets.lo_entry, buckets.w_lo);
    if(buckets.lo != buckets.hi) accumulate(buckets.hi_entry, buckets.w_hi);
}

int ShoeComp::random_draw(mt19937& rng){
    uniform_int_distribution<int> dist(0, cards_left - 1);
    int r = dist(rng);
    for(int rank = 0; rank < RANKS; ++rank){
        r -= shoe[rank];
        if(r < 0) {draw(rank); return rank;}
    }
    __builtin_unreachable();
}

void ShoeComp::sample_at_tc(double target_tc, double depth, mt19937& rng){
    assert(cards_left == total_cards); //make sure the shoe is shuffled

    int cards_dealt = (int)round(total_cards * depth);
    float decks_left = (total_cards - cards_dealt) / 52.0f;
    int target_rc = (int)round(target_tc * decks_left);

    int total_low = 0, total_neu = 0, total_high = 0;
    for (int r : LOW_RANKS)  total_low  += new_shoe[r];
    for (int r : NEU_RANKS)  total_neu  += new_shoe[r];
    for (int r : HIGH_RANKS) total_high += new_shoe[r];

    auto sample_hypergeom = [&](int K, int N, int n) -> int {
        // K successes in population N, draw n without replacement
        int x = 0;
        int remaining_K = K, remaining_N = N;
        for (int i = 0; i < n; ++i) {
            uniform_real_distribution<double> u(0.0, 1.0);
            if (u(rng) < (double)remaining_K / remaining_N) {
                ++x; --remaining_K;
            }
            --remaining_N;
        }
        return x;
    };

    int neu_dealt = sample_hypergeom(total_neu, total_cards, cards_dealt);
    int lh_dealt = cards_dealt - neu_dealt;

    int low_min = max(0, lh_dealt - total_high);
    int low_max = min(total_low, lh_dealt);
    int low_ideal = (lh_dealt + target_rc) / 2;
    int low_dealt = clamp(low_ideal, low_min, low_max);
    int high_dealt = lh_dealt - low_dealt;
    assert(lh_dealt <= total_low + total_high);

    auto distribute = [&](auto& ranks, int group_total_dealt) {
        int remaining_dealt = group_total_dealt;
        int remaining_pool = 0;
        for (int r : ranks) remaining_pool += shoe[r];
        for (size_t i = 0; i < ranks.size(); ++i) {
            int r = ranks[i];

            int dealt_r = 0;
            if (i == ranks.size() - 1) dealt_r = remaining_dealt;
            else dealt_r = sample_hypergeom(shoe[r], remaining_pool, remaining_dealt);

            int max_dealt = max(0, shoe[r] - 2);
            dealt_r = min(dealt_r, max_dealt);
            shoe[r] -= dealt_r;
            remaining_pool -= shoe[r] + dealt_r;  // pool of remaining ranks
            remaining_dealt -= dealt_r;
        }
    };

    distribute(NEU_RANKS,  neu_dealt);
    distribute(LOW_RANKS,  low_dealt);
    distribute(HIGH_RANKS, high_dealt);

    int actual_left = 0;
    for(int r = 0; r < RANKS; r++) actual_left += shoe[r];
    cards_left = actual_left;

   //cards_left = total_cards - cards_dealt;
}

void EV_Calculator::generate_base(){
    uint32_t total_entries = NUM_TC_BUCKETS * (int)HandCat::NUM_HANDS * RANKS;
    EVT.entries.assign(total_entries, ev_entry{});
    int cut_off = (rules.cut_depth * 52); 
    mt19937 rng{random_device{}()};
    //uniform_real_distribution<double> jitter(-TC_BUCKET_SIZE/2, TC_BUCKET_SIZE/2);

    #pragma omp parallel for collapse(2) schedule(dynamic)
    for(int b = 0; b < NUM_TC_BUCKETS; ++b){
        for(int trial = 0; trial < BASE_TRIALS; ++trial){
            #ifdef _OPENMP
            int tid = omp_get_thread_num();
            #else
                int tid = 0;
            #endif
            ShoeComp SC(rules.decks);
            std::mt19937 rng{std::random_device{}() + (uint32_t)(tid * 2654435761u)};

            uniform_real_distribution<double> jitter(-TC_BUCKET_SIZE / 2, TC_BUCKET_SIZE / 2);

            float target_tc = TC_MIN + b * TC_BUCKET_SIZE + jitter(rng); 
            SC.sample_at_tc(target_tc, REF_DEPTH, rng);
            if(tid == 0)printf("Trial %d:\n\tTarget TC = %.2f\n\tActual TC = %.4f\n", trial, target_tc, SC.true_count());
            int seen[(int)HandCat::NUM_HANDS] = {0};
            for(int i = 0; i < RANKS; i++){ // get one sample for every Hand possible                
                if(SC.shoe[i] == 0) continue; //don't draw cards that are gone
                SC.draw(i); 

                array<double, 6> p_dealer = dealer_probs(i, SC); //precompute dealer-hand probabilities
                for(int c1 = 0; c1 < RANKS; c1++){
                    if(SC.shoe[c1] == 0) continue;

                    SC.draw(c1);
                    for(int c2 = c1; c2 < RANKS; c2++){
                        if(SC.shoe[c2] == 0) continue;
                        SC.draw(c2);

                        Hand player;
                        player.add_card(c1);
                        player.add_card(c2);

                        if(player.total == 21) {SC.undraw(c2); continue;}
                        int hc = (int)player.categorize();
                        if(!seen[hc]){
                            calculate_base_ev(player, i, SC, p_dealer, target_tc);
                            seen[hc] = 1;
                        }
                        SC.undraw(c2);
                    }
                    SC.undraw(c1);
                }
                SC.undraw(i);
            }

            if(tid == 0)printf("Walking through deck at TC = %.2f\n", SC.true_count());
            array<double, 4> tmp_evs;
            while(SC.cards_left > cut_off){//Finish dealing from the sampled composition to cover more hands
                int dealer_up = SC.random_draw(rng);
                int c1 = SC.random_draw(rng);
                int c2 = SC.random_draw(rng);
                
                Hand player;
                player.add_card(c1);
                player.add_card(c2);
                if(player.total == 21) continue; //Don't calculate for Naturals
                if(tid ==0) printf("Calculating evs for %d, %d vs dealer %d\n", vals[c1], vals[c2], vals[dealer_up]);
                tmp_evs = calculate_ev(player, dealer_up, SC);
            }
            SC.shuffle();
        }
    }
    EVT.save_base();
}

array<double, 4> EV_Calculator::calculate_ev(Hand player, int dealer_up, ShoeComp& shoe){
    array<double, 6> p_dealer = dealer_probs(dealer_up, shoe);
    array<double, 4> evs;
    evs[STAND] = stand(player, p_dealer);
    evs[HIT] = hit(player, dealer_up, shoe, p_dealer);
    evs[DOUBLE] = dd(player, dealer_up, shoe, p_dealer);
    evs[SPLIT] = split(player, dealer_up, shoe);

    EVT.update(evs, player, dealer_up, shoe.true_count());
    return evs;
}

array<double, 4> EV_Calculator::calculate_base_ev(Hand player, int dealer_up, ShoeComp& shoe, array<double, 6> &p_dealer, double tc_override= NAN){
    double tc = std::isnan(tc_override) ? shoe.true_count() : tc_override;

    array<double, 4> evs;
    evs[STAND] = stand(player, p_dealer);
    evs[HIT] = hit(player, dealer_up, shoe, p_dealer);
    evs[DOUBLE] = dd(player, dealer_up, shoe, p_dealer);
    evs[SPLIT] = split(player, dealer_up, shoe);

    EVT.update(evs, player, dealer_up, tc);
    return evs;
}

array<double, 4>EV_Calculator::lookup_ev(Hand hand, int dealer_up, ShoeComp &shoe){
    calculate_ev(hand, dealer_up, shoe); //Calculate new EV and add it to the table before returning a value

    double tc = shoe.true_count();
    ev_buckets buckets(EVT, hand, dealer_up, tc);

    array<double, 4> evs;
    for(auto move : MOVES){
        double lo_w = buckets.lo_entry->ev_weight[move];
        double hi_w = buckets.hi_entry->ev_weight[move];

        if(lo_w == 0 && hi_w == 0){evs[move] = NONE; continue;}

        double w_lo = lo_w > 0 ? buckets.w_lo : 0.0;
        double w_hi = hi_w > 0 ? buckets.w_hi : 0.0;

        double lo_ev = (lo_w > 0) ? w_lo * (buckets.lo_entry->ev_sum[move] / lo_w) : 0.0;
        double hi_ev = (hi_w > 0) ? w_hi * (buckets.hi_entry->ev_sum[move] / hi_w) : 0.0;

        evs[move] = (hi_ev + lo_ev) / (w_lo + w_hi);
    }
    return evs;
}

array<double, 6> EV_Calculator::dealer_probs(int up, ShoeComp &shoe){
    array<double, 6> probs = {0, 0, 0, 0, 0, 0};
    Hand dealer;
    dealer.add_card(up);

    //first draw is conditioned on the dealer NOT having blackjack
    array<double, 6> next;
    Hand d_next;
    int total = shoe.cards_left;
    if(up == R_A) total -= shoe.shoe[R_T];
    if(up == R_T) total -= shoe.shoe[R_A];
    for(int i = 0; i < RANKS; i++){
        if(up == R_A && i == R_T) continue;
        if(up == R_T && i == R_A) continue;
        if(shoe.shoe[i] == 0) continue;

        double p = (double)shoe.shoe[i] / total;

        d_next = dealer;
        d_next.add_card(i);
        shoe.draw(i);
        next = dealer_probs(d_next, shoe);
        for(int j = 0; j < 6; j++) probs[j] += next[j] * p;
        shoe.undraw(i);
    }
    return probs;
}

array<double, 6> EV_Calculator::dealer_probs(const Hand& dealer, ShoeComp& shoe){

    if(!dealer.soft && dealer.total == 17) return {1, 0, 0, 0, 0, 0};
    if(dealer.total == 18) return {0, 1, 0, 0, 0, 0};
    if(dealer.total == 19) return {0, 0, 1, 0, 0, 0};
    if(dealer.total == 20) return {0, 0, 0, 1, 0, 0};
    if(dealer.total == 21) return {0, 0, 0, 0, 1, 0};
    if(dealer.total >  21) return {0, 0, 0, 0, 0, 1};
    

    array<double, 6> probs = {0, 0, 0, 0, 0, 0};
    array<double, 6> next;
    Hand d_next;
    for(int i = 0; i < RANKS; i++){
        if(shoe.shoe[i] == 0) continue;
        double p = shoe.p_rank(i);
        d_next = dealer;
        d_next.add_card(i);
        shoe.draw(i);
        next = dealer_probs(d_next, shoe);
        for(int j = 0; j < 6; j++) probs[j] += next[j] * p;
        shoe.undraw(i);
    }

    return probs;
}

double EV_Calculator::stand(Hand player, array<double, 6> &p_dealer){
    if (player.total > 21) return -1.0;

    double ev = 0.0;
    for(int i = 0; i < 5; i++){
        int dtotal = 17 + i;
        if(player.total < dtotal) ev -= p_dealer[i];
        if(player.total > dtotal) ev += p_dealer[i];
    }
    ev += p_dealer[5];
    return ev;
}

double EV_Calculator::hit(Hand player, int dealer_up, ShoeComp& shoe, array<double, 6> &p_dealer){
    if(player.total > 21) return -1.0;

    Hand next;
    double ev = 0.0;
    for(int i = 0; i < RANKS; i++){
        if(shoe.shoe[i] == 0) continue;
        
        double p = shoe.p_rank(i);
        next = player;
        next.add_card(i);
        shoe.draw(i);
        double ev_next = max(stand(next, p_dealer), hit(next, dealer_up, shoe, p_dealer));
        ev += ev_next * p;
        shoe.undraw(i);
    }
    return ev;
}

double EV_Calculator::dd(Hand player, int dealer_up, ShoeComp& shoe, array<double, 6>& p_dealer){
    if(!player.can_double) return NONE;
    Hand next;
    double ev = 0;
    for(int i = 0; i < RANKS; i++){
        if(shoe.shoe[i] == 0) continue;
        double p = shoe.p_rank(i);
        next = player;
        next.add_card(i);
        shoe.draw(i);
        ev += p * stand(next, p_dealer);
        shoe.undraw(i);
    }
    return ev * 2.0;
}

double EV_Calculator::split(Hand player, int dealer_up, ShoeComp& shoe){ //TODO: Calculate full depth split EV
    if(player.pair < 0) return NONE;
    if(player.split_hands + 1 > rules.max_splits) return NONE;    
    int pair_rank = player.pair;
    double ev = 0.0;
    for(int i = 0; i < RANKS; i++){
        if(shoe.shoe[i] == 0)continue;
        
        double p = shoe.p_rank(i);
        shoe.draw(i);
        
        Hand next;
        next.add_card(pair_rank);
        next.add_card(i);
        next.split_hands += player.split_hands;
        
        array<double,6> p_dealer_branch = dealer_probs(dealer_up, shoe);

        double ev_stand = stand(next, p_dealer_branch);
        double ev_hit = hit(next, dealer_up, shoe, p_dealer_branch);
        double hand_ev = max(ev_stand, ev_hit);
        if (rules.DAS){
            double ev_double = dd(next, dealer_up, shoe, p_dealer_branch);
            hand_ev = max(ev_double, hand_ev);
        }
        if(next.pair >= 0){            
            double ev_resplit = split(next, dealer_up, shoe);
            hand_ev = max(ev_resplit, hand_ev);
        }

        ev += p * hand_ev;
        shoe.undraw(i);
    }
    return ev * 2.0;
}




