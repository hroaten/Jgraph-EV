#include<string>
#include<vector>
using namespace std;
#ifndef BJ_H
#define BJ_H
typedef float EV[4];

/*
//game conditions lookup key for player EV's
typedef struct game_condition{
    float count; //should be rounded to 0.5
    int split; 
    uint8_t total;
    int soft; 
    char upcard; 
} condtion;
*/
struct card{
    string value;
    char suit; //suit only used for jgraph GUI
};

struct Hand{
    vector<card> hand;
    bool hard;
    bool split;
    int total;
    EV evs;
};

class Shoe {
    vector<card> shoe;
    bool stop_shoe;
    public :
    void Shuffle(float);
    Shoe(int, float);
};

class Dealer{
    public: 
    card up; //dealer up card
    vector<card> hand; //dealer's hand 
    vector<card> shoe; //shoe to deal from/shuffle
    int p_idx;
    bool shuffle_next;

    Dealer(int decks, float pen);
    void Shuffle(float pen);

    void Deal();
    void play();
    void pay();
};

class Player{
    double bet;
    Hand hand;
    void hit();
    void stand();
    void split();
    void double_down();
};

#endif
