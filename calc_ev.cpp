#include "calc_ev.h"

/*
#define HIT 0
#define STAND 1
#define DOUBLE 2
#define SPLIT 3
*/
using namespace std;

struct entry{
state s;
EV e;
};


/*

Maybe need to make a seperate file of Dealer probabilities for each Ruleset? initials will be the same,
but making recursive calls to DP on every execution seems exvessive

*/


Calc_EV::Calc_EV(string rules){
    ruleset = rules;
    string file = "calculated_EVs/" + rules + ".evs";
    ev_file.open(file, ios::in | ios::out | ios::binary | ios::app);
    if(!ev_file.is_open()) printf("Failed to open %s\n", file.c_str()); 
    char test[3];
    ev_file.seekg(0);
    ev_file.read(test, 3);
    if(ev_file.gcount() != 3 || memcmp(test, MAGIC, 3) != 0){
        printf("Magic is: %s (%lu bytes)\n header contained: %s(%lu)\n", MAGIC, sizeof(MAGIC), test, sizeof(test));
        printf("NEW FILE\n");
        ev_file.clear();
        ev_file.write(MAGIC, 3);
        ev_file.flush();
        basic_evs();
    }

    entry e;
    while(ev_file.read(reinterpret_cast<char*>(&e), sizeof(e))) evs.emplace(e.s,e.e);
};

pair<state, vector<float>> Calc_EV::dealer_probs(state s){
    //ruleset stuff 
    int possibles = 7; //get from ruleset later
    float *probs = (float *)calloc(possibles, sizeof(float)); //[17, 18, 19, 20, 21, 22, BJ]

    //base cases
    if(s.total == 17 && !s.soft) {probs[0] = 1; probs[1] = 0; probs[2] = 0;  probs[3] = 0; probs[4] = 0; probs[5] = 0; } //change for S17 rules
    if(s.total == 18) {probs[0] = 0; probs[1] = 1; probs[2] = 0; probs[3] = 0; probs[4] = 0; probs[5] = 0; } 
    if(s.total == 19) {probs[0] = 0; probs[1] = 0; probs[2] = 1; probs[3] = 0; probs[4] = 0; probs[5] = 0; }
    if(s.total == 20) {probs[0] = 0; probs[1] = 0; probs[2] = 0; probs[3] = 1; probs[4] = 0; probs[5] = 0; }
    if(s.total == 21) {probs[0] = 0; probs[1] = 0; probs[2] = 0; probs[3] = 0; probs[4] = 1; probs[5] = 0; }
    if(s.total > 21)  {probs[0] = 0; probs[1] = 0; probs[2] = 0; probs[3] = 0; probs[4] = 0; probs[5] = 1; } //Bust
    
    vector<float> ret(probs, probs + sizeof(probs) / sizeof(float));
    free(probs);
    make_pair(s, ret);

    int low[5] = {'2','3','4','5','6'};
    int neutral[3] = {'7','8','9'};
    int high[5] = {'T','T','T','T', 'A'};

    //do some count stuff somewhere 
    //TODO Seperate basic strategy from count calculations
    vector<float> temp(possibles, 0);
    for(auto l : low){
        state ns = s;
        ns.total += (l - '0');
        if(ns.soft && ns.total > 21) {ns.soft = 0; ns.total -= 10;}
        temp = dealer_probs(ns).second;
        for(int i = 0; i < possibles; i++){
            ret[i] += temp[i]; //division here? maybe no for count
        }
    }
    for(auto n : neutral){
        state ns = s;
        ns.total += (n - '0');
        if(ns.soft && ns.total > 21) {ns.soft = 0; ns.total -= 10;}

        temp = dealer_probs(ns).second;
        for(int i = 0; i < possibles; i++){
            ret[i] += temp[i]; 
        }
    }
    for(auto h : high){
        state ns = s;
        if(h == 'T'){
            if(ns.soft) ns.soft = 0; //soft total + 10 = hard total
        }
        else{
            if(ns.total + 11 <= 21){ns.total += 11; ns.soft = 1;}
            else {ns.total += 1; ns.soft = 0;}
        }
        //recursion

        temp = dealer_probs(ns).second;
        for(int i = 0; i < possibles; i++){
            ret[i] += temp[i]; 
        }
    }



};  

void Calc_EV::basic_evs(){
    state base;
    char cards[13] {'2','3','4','5','6','7','8','9','T','T','T','T', 'A'};
    for (auto card : cards){
        base.count = 0.0;
        base.dealer = card;
        if(card == 'A') {base.total = 11; base.soft = 1;}
        else if(card == 'T') {base.total = 10; base.soft = 0;}
        else {base.total = card - '0'; base.soft = 0;}
        //given a value for hashing purposes 
        base.dd = -1;
        base.split = -1;
        dp.emplace(card , dealer_probs(base).second);
    }
};

void Calc_EV::write_ev(state s, EV ev){
    entry e = {s, ev};
    ev_file.write(reinterpret_cast<char*> (&e), sizeof(e));
};

EV Calc_EV::calc(state s){
    //try lookup
    auto it = evs.find(s);
    if(it != evs.end()) {
        EV value = it->second;
        return value;
    }

    //lookup fails, calculate new EV
    EV e;

    s.dealer == 'A' ? e[INS] = ins(s) : NONE;
    s.dd == 1 ? e[DOUBLE] = dd(s) : NONE;
    s.split == 1 ? e[SPLIT] = split(s) : NONE;

    e[STAND] = stand(s);
    e[HIT] = hit(s);


    evs.emplace(s,e);
    write_ev(s, e);
    return e;
};

float Calc_EV::count_factor(float count, float decks_left){
    return 1.0; // no counting first
};


float Calc_EV::stand(state s){
    //base cases
    if(s.total > 21 && s.soft == 0) return -1; //bust
    if(s.total == 21 && s.dealer != 'A' && s.dealer != 'T') return 1.5; //Blackjack

    float k = count_factor(s.count, s.decks_left);
    float ev;

    return ev;
};

float Calc_EV::hit(state s){
    if(s.total >= 21) return -1;

    float k = count_factor(s.count, s.decks_left);
    float ev;
    state ns;
    EV low;
    EV neutral;
    EV high;
    EV A;

    ns.dealer = s.dealer;
    ns.dd = 0;
    ns.split = 0;
    ns.decks_left = s.decks_left;

    //low card ev
    for (int i = 2; i < 7; i++){
        ns.total = s.total + i;
        ns.count = s.count + (2.0f * (1 / s.decks_left)) / 2.0f;
        ns.soft = s.soft;
        low = calc(ns);
    }
    //neutral card ev
    for(int i = 7; i < 10; i++){
        ns.total = s.total + i;
        ns.count = s.count;
        if (s.soft == 1 && ns.total > 21) { ns.total -= 10; ns.soft = 0;}
        else ns.soft = s.soft;
        neutral = calc(ns); 
    }
    //ev for drawing a 10
        ns.total = s.total + 10;
        ns.count = s.count - (2.0f * (1 / s.decks_left)) / 2.0f;
        ns.soft = 0;
        high = calc(ns);

    //EV for drawing an Ace
        if(s.total < 10) {ns.total = s.total + 11; ns.soft = 1;}
        else {ns.soft = 0; ns.total = s.total + 1;}
        ns.count = s.count - (2.0f * (1 / s.decks_left)) / 2.0f;
        A = calc(ns);

    
    return ev;
};

float Calc_EV::dd(state s){
    if (s.total >= 21) return -2;

    float k = count_factor(s.count, s.decks_left);
    float ev;
    EV low, neutral, high, A;
    state ns;
    ns.decks_left = s.decks_left;
    ns.dd = 0;
    ns.split = 0;
    ns.soft = s.soft;
    ns.dealer = s.dealer;

    //low cards
    for(int i = 2; i < 7; i++){
        ns.total = s.total + i;
        ns.count = s.count + (2.0f * (1 / s.decks_left)) / 2.0f;
        low = calc(ns);
    }
    //neutral cards
    for(int i = 7; i < 10; i++){
        ns.total = s.total + i;
        ns.count = s.count;
        if (s.soft == 1 && ns.total > 21) { ns.total -= 10; ns.soft = 0;}
        else ns.soft = s.soft;
        neutral = calc(ns); 
    }

    //tens
    ns.total = s.total + 10;
    ns.count = s.count - (2.0f * (1 / s.decks_left)) / 2.0f;
    ns.soft = 0;
    high = calc(ns);

    //Ace
    if(s.total < 10) {ns.total = s.total + 11; ns.soft = 1;}
    else {ns.soft = 0; ns.total = s.total + 1;}
    ns.count = s.count - (2.0f * (1 / s.decks_left)) / 2.0f;
    A = calc(ns);

    //apply count factor 
    //ev = 2 * weighted stand

    return ev;
};

float Calc_EV::split(state s){
    
    float k = count_factor(s.count, s.decks_left);
    float ev;

    EV h1, h2;
    state s1, s2;
    s1.total = s.total / 2;
    s1.dd = 1;
    s1.count = s.count; //need to think about how/when to update counts
    s1.soft = s.soft;
    s1.dealer = s.dealer;
    s1.decks_left = s.decks_left; // will need an adjustment for multi-deck shoes
    s1.split = 1; // will need to update splitability

    s2.total = s.total / 2;
    s2.dd = 1;
    s2.count = s.count; //need to think about how/when to update counts
    s2.soft = s.soft;
    s2.dealer = s.dealer;
    s2.decks_left = s.decks_left; // will need an adjustment for multi-deck shoes
    s2.split = 1; // will need to update splitability
    
    //calculate evs for both hands
    h1 = calc(s1); 
    h2 = calc(s2); 

    for(int i = 0; i < 4; i++){
        if(h1[i] != NONE) ev += h1[i];
        if(h2[i] != NONE) ev += h2[i];
    }
    //average expected value of hands? 
    return ev;
};

float Calc_EV::ins(state s){

    float k = count_factor(s.count, s.decks_left);
    float ev = 0;
    /*
    ev(not) = calc(some state) - .5
    ev(BJ) = 0;
    ev(bj) > not ? ev = 0 : ev(not)
    */
    return 0;
};

/*
Calc_EV::~Calc_EV(){
    fout.close();
}
*/