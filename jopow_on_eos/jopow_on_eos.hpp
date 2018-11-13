#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/crypto.h>

#include <vector>
#include <sstream>
#include <string>

using namespace eosio;

CONTRACT jopow_on_eos: public eosio::contract
{
  public:
    using contract::contract;
    //Actions
    //A new game is created
    ACTION creategame(const name &challenged, const name &host, const asset &quantity, const std::string &hashed_move_str);

    //A chellenger accepts a game
    ACTION acceptgame(const uint64_t &id, const name &challenged, const asset &quantity, const std::string &hashed_move_str);

    //Close game and remove from ram
    ACTION close(const uint64_t &id, const name &by);

    //The account that wants to make a move
    ACTION move(const uint64_t &id, const name &by, const std::string &hashed_move_str);

    //Reveals the players seed and checks who wins
    ACTION reveal(const uint64_t &id, const std::string &seed_host, const std::string &seed_challenged);

    //On transfer function
    ACTION ontransfer(const name &from,const name &to,const asset &quantity,const std::string &memo);    
 
  public:

    TABLE game
    {
        game()
        {
            initialize();
        }
        uint64_t id;
        name challenged;
        name host;
        asset quantity;
        name winner = name("none");
        uint8_t host_move;
        capi_checksum256 host_move256;
        uint8_t challenged_move;
        capi_checksum256 challenged_move256;
        bool can_reveal;

        void initialize()
        {
            host_move = 0;
            challenged_move = 0;
            winner = name("none");
            can_reveal = false;
        }

        void start_game()
        {
            initialize();
        }

        auto primary_key() const { return id; }
        uint64_t byhost() const { return host.value; }
        uint64_t bychallenged() const { return challenged.value; }

        EOSLIB_SERIALIZE(game, (id)(challenged)(host)(quantity)(winner)(host_move)(host_move256)(challenged_move)(challenged_move256)(can_reveal))
    };

    // Table that stores a list of games
    typedef eosio::multi_index<"games"_n, game, 
                                indexed_by<"byhost"_n, const_mem_fun<game, uint64_t, &game::byhost>>,
                                indexed_by<"bychallenged"_n, const_mem_fun<game, uint64_t, &game::bychallenged>>
                            > games;
};


