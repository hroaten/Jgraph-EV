#include<vector>
#include<array>
#include<string>
#include<cassert>
#include<algorithm>
#include<string.h>
#include<math.h>
#include<filesystem>
#include<random>
#include<unordered_map>
using namespace std;
namespace fs = std::filesystem;


#ifndef BJ_H
#define BJ_H
//indices of EV arrray(s)
#define STAND 0
#define HIT 1
#define DOUBLE 2
#define SPLIT 3
//indices of card values
#define RANKS 10
#define R_2  0
#define R_3  1
#define R_4  2
#define R_5  3
#define R_6  4
#define R_7  5
#define R_8  6
#define R_9  7
#define R_T  8  
#define R_A  9

//True counts used for calculations
constexpr double TC_BUCKET_SIZE = 0.5;
constexpr double TC_MIN = -5.0;
constexpr double TC_MAX = 5.0;
constexpr int NUM_TC_BUCKETS = (int)((TC_MAX - TC_MIN) / TC_BUCKET_SIZE) + 1;

constexpr array<int, 4> MOVES = {STAND, HIT, DOUBLE, SPLIT};

static constexpr array<int, 5> LOW_RANKS  = {R_2, R_3, R_4, R_5, R_6};
static constexpr array<int, 3> NEU_RANKS  = {R_7,R_8, R_9};
static constexpr array<int, 2> HIGH_RANKS = {R_T, R_A};

static constexpr array<int, RANKS> vals = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11}; //rank values

enum class HandCat : uint8_t {
    // Hard totals: 4 through 21
    HARD_4 = 0,
    HARD_5,
    HARD_6,
    HARD_7,
    HARD_8,
    HARD_9,
    HARD_10,
    HARD_11,
    HARD_12,
    HARD_13,
    HARD_14,
    HARD_15,
    HARD_16,
    HARD_17,
    HARD_18,
    HARD_19,
    HARD_20,
    HARD_21,        

    SOFT_13,  
    SOFT_14,
    SOFT_15,
    SOFT_16,       
    SOFT_17,        
    SOFT_18,        
    SOFT_19,        
    SOFT_20,
    SOFT_21,

    PAIR_2,
    PAIR_3,
    PAIR_4,
    PAIR_5,
    PAIR_6,
    PAIR_7,
    PAIR_8,
    PAIR_9,
    PAIR_T,
    PAIR_A,

    NUM_HANDS
};

//used for EVS of double or split when not possible
constexpr double NONE = -std::numeric_limits<double>::infinity();

struct ruleset{
    string name = "basic";
    uint8_t decks = 6; 
    double cut_depth = 0.5; //how many cards are cut off, as # of decks
    uint8_t max_splits = 4; //how many hands one player can have from consecutive splits
    uint8_t DAS = 1;
    uint8_t H17 = 1; 
    uint8_t surr = 0; 
    uint8_t Ins = 0;
};

struct ev_entry{ 
    //All elements indexed based on move
    double ev_sum[4]; //Cumulative sum of EV's
    double ev_weight[4]; //Cumulative sum of weights 
    double ev_sq_sum[4]; //Cumulative sum of weight * ev^2 to use for variance in future versions
};

class Hand{
    public:
    uint8_t total;
    uint8_t soft;
    uint8_t can_double;
    int8_t pair;
    uint8_t split_hands; //how many hands have been made from splitting
    uint8_t n_cards;
    Hand(){
        total = 0;
        soft = 0;
        can_double = 1;
        pair = -1; //-1 means no pair, other values translate to pair RANK
        split_hands = 1;
        n_cards = 0;
    }

    const HandCat categorize(){
        if(pair >= 0) return (HandCat)((int)HandCat::PAIR_2 + pair);
        else if(soft) return (HandCat)((int)HandCat::SOFT_13 + total - 13);
        else return (HandCat)((int)HandCat::HARD_4 + total - 4);
    }

    void add_card(int rank){
        if(n_cards == 1 && total == vals[rank]) pair = rank; //make pairs 
        total += vals[rank];
        if(rank == R_A) soft++;
        while(total > 21 && soft > 0){
            total -= 10;
            soft--;
        }
        n_cards++;
        if(n_cards > 2) can_double = 0;
    }
};

// struct DealerKey {
//     array<uint8_t, RANKS> shoe;
//     uint8_t total;
//     uint8_t soft;
//     bool operator==(const DealerKey& o) const noexcept {
//         return total == o.total && soft == o.soft && shoe == o.shoe;
//     }
// };
// struct DealerKeyHash {
//     size_t operator()(const DealerKey& k) const noexcept {
//         // FNV-1a over the packed bytes
//         size_t h = 1469598103934665603ull;
//         auto mix = [&](uint8_t b){ h ^= b; h *= 1099511628211ull; };
//         for (uint8_t b : k.shoe) mix(b);
//         mix(k.total); mix(k.soft);
//         return h;
//     }
// };

class EVTable{
    public:
    vector<ev_entry> entries;


    EVTable(string);

    ev_entry &at(HandCat hc, int dealer_up, int tc_bucket){
        int h = (int)hc;
        return entries[(h * RANKS + dealer_up) * NUM_TC_BUCKETS + tc_bucket];
    }
    void update(array<double, 4>&, Hand&, int, double);
    void load();//load stored EV's from disk
    void save(); //save stored EV's to disk
    void save_base(); //save base EV's to disk
    void load_base(); //load base EV's from disk (used for strategy plots)
    //private:
    string ev_file;
    string base_file;
};

class ev_buckets{
    public:
        int lo;
        int hi;
        double w_lo;
        double w_hi;
        ev_entry *hi_entry;
        ev_entry *lo_entry;
        ev_buckets(EVTable&, Hand&, int, double);
};

class ShoeComp{ //Shoe Composition
    public:
    
    array<int, RANKS> shoe;
    int total_cards;
    int cards_left;
    ShoeComp(int);

    int running_count(){
        int count = 0;
        for(int rank = 0; rank < RANKS; ++rank) count += shoe[rank] * hilo[rank];
        return count;
    }

    double true_count(){return running_count() / (cards_left / 52.0f); }

    double p_rank(int rank){return (double)shoe[rank] / cards_left;}

    void draw(int rank){
        assert(shoe[rank] > 0);
        shoe[rank]--;
        cards_left--;
    }

    void undraw(int rank){shoe[rank]++; cards_left++;}
    void shuffle();

    void sample_at_tc(double, double, mt19937 &);
    int random_draw(mt19937 &); //randomly picks a card rank to draw, used in base EV generations

    //private : 
    static constexpr array<int, RANKS> hilo = {-1, -1, -1, -1, -1, 0, 0, 0, 1, 1}; //How each rank affects the count
    array<int, RANKS> new_shoe; //copied to shoe on each shuffle 
};

class EV_Calculator{
    public:
    array<double, 4> lookup_ev(Hand, int, ShoeComp &);
    EV_Calculator(ruleset r) : rules(r) , EVT(rules.name) {}
    void generate_base(); //TODO: Make private for actual implementation

    //private: 
    array<double, 4> calculate_ev(Hand, int, ShoeComp&);

    ruleset rules;
    EVTable EVT;
    
    double stand(Hand, array<double, 6>&);
    double hit(Hand, int, ShoeComp&, array<double, 6>&);
    double dd(Hand, int, ShoeComp&, array<double, 6>&);
    double split(Hand, int, ShoeComp&);

    array<double, 6> dealer_probs(int, ShoeComp&);
    array<double, 6> dealer_probs(const Hand&, ShoeComp&);
    array<double, 4> calculate_base_ev(Hand, int, ShoeComp&, array<double, 6>&, double);

    //void generate_base();
};

class BlackJack{
    //Actual Gameplay
    // void deal();
    // void shuffle();

    public:
    BlackJack(ruleset);

    Hand player;
    Hand dealer;
    ShoeComp SC;
    
    void deal();
    void shuffle();
};

#endif
