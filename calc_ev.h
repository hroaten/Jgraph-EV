#include<iostream>
#include<cstring>
#include<fstream>
#include<unordered_map>
#include<algorithm>

#ifndef CALC_EV_H
#define CALC_EV_H
//indices for EV struct
#define HIT 0
#define STAND 1
#define DOUBLE 2
#define SPLIT 3
#define INS 4
using namespace std;

using EV = array<float, 5>;
constexpr float NONE = -std::numeric_limits<float>::infinity();
constexpr char MAGIC[3] = {'B', 'J' , '!'};

struct state{
    float decks_left; 
    int rc;

    float count;
    int total;
    char dealer;
    short soft;
    short split;
    short dd;

    short decks; //shoe size;
    float pen;

    /*
    TODO:
    //calculate true count, round to .5 via running count / decks left
    */

    bool operator==(const state &other) const {

        return  count == other.count &&
                total == other.total &&
                dealer == other.dealer &&
                soft  == other.soft  &&
                split == other.split &&
                dd == other.dd;
                
    }
};

struct statehash{
    size_t operator()(const state &s) const {
        size_t h1, h2, h3, h4, h5, h6;
        h1 = hash<float>{}(s.count);
        h2 = hash<int>{}(s.total);
        h3 = hash<char>{}(s.dealer);
        h4 = hash<short>{}(s.soft);
        h5 = hash<short>{}(s.split);
        h6 = hash<short>{}(s.dd);

        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4) ^ (h6 << 5);
    }
};

class Calc_EV{
    string ruleset; //need to create naming convention for rulesets,
    unordered_map<state, EV, statehash> evs;
    unordered_map<char, vector<float>> dp;

    fstream ev_file;

    float dd(state);
    float stand(state);
    float hit(state);
    float split(state);
    float ins(state);

    void write_ev(state, EV); //write newly calculate ev to cache file
    void basic_evs(); //generate Basic strategy for given ruleset
    pair <state, vector<float>> dealer_probs(state); //calculate the probabilities for dealer hands given current game state

    public:
    Calc_EV(string rules="basic");
    EV calc(state);
};

#endif