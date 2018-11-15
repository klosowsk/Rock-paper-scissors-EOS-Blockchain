#include "jopow_on_eos.hpp"

using namespace eosio;
/*
*  For the following jokey pow game:
*       - Each pair of player can have 2 unique game, one where player_1 become host and player_2 become challenged and vice versa
*       - The game data is stored in the "host" scope and use the "challenged" as the key
*  
*  Where:
*       - 0 not played yet
*       - 1 rock
*       - 2 paper
*       - 3 scissor
* 
*  In order to deploy this contract:
*       - Create an account called jopow.on.eos
*       - Add jopow.on.eos key to your wallet
*       - Set the contract on the jopow.on.eos account
*       - Add code permission for inline actions
* 
*  How to play the game:
*        - Create a game using `creategame` action, with you as the host and wait the challenged, specify the value of the challenge.
*        - Accept a game challenge using `acceptgame` action
*        - Use the `move` action to make a move by specifying rock, paper or scissors.
*        - Ask the challenged to make a move
*        - Check if someone wins or draw.
*        - If you want to clear the game from the database to save up some space after the game has ended, use the `close` action.
*
*   DONE 
*        - Create a new game and challenge another player
*        - Accept a game
*        - Player one makes a move
*        - Player two makes a move
*        - Moves validation
*        - Checks for winner or draw
*        - Allow a token contract to handle all the game logic through transfers
*   TODO: 
*        - By now the game accepts only one round
*        - Allow best of three and other modalities
*        - Write ricardian contracts
*/


/*
    Auxiliar functions
*/
//Check if a movement is valid, by default the previous move must be 0 to be accepted a new move

//Check if there is a winner
eosio::name get_winner(const jopow_on_eos::game& current_game){
    auto& host_move = current_game.host_move;
    auto& challenged_move = current_game.challenged_move;

    if( host_move == 0 || challenged_move == 0 ){
        return name("none");
    } else if ( host_move == challenged_move ) {
        return name("draw");
    } else if( host_move == 1 ){
        if( challenged_move == 3 ) return current_game.host; //Rock beats Scissors
        if( challenged_move == 2 ) return current_game.challenged; //Paper beats Rock
    } else if( host_move == 2 ){
        if( challenged_move == 1 ) return current_game.host; //Paper beats Rock
        if( challenged_move == 3 ) return current_game.challenged; //Scissors beats Paper
    } else if( host_move == 3 ) {
        if( challenged_move == 1 ) return current_game.challenged; //Rock beats Scissors
        if( challenged_move == 2 ) return current_game.host; //Scissors beats Paper
    } else {
        return name("error");
    }
    return name("error");
}

// From hex to capi_checksum256
uint8_t from_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    eosio_assert(false, "Invalid hex character");
    return 0;
}

size_t from_hex(const std::string& hex_str, char* out_data, size_t out_data_len) {
    auto i = hex_str.begin();
    uint8_t* out_pos = (uint8_t*)out_data;
    uint8_t* out_end = out_pos + out_data_len;
    while (i != hex_str.end() && out_end != out_pos) {
        *out_pos = from_hex((char)(*i)) << 4;
        ++i;
        if (i != hex_str.end()) {
            *out_pos |= from_hex((char)(*i));
            ++i;
        }
        ++out_pos;
    }
    return out_pos - (uint8_t*)out_data;
}

capi_checksum256 hex_to_sha256(const std::string& hex_str) {
    eosio_assert(hex_str.length() == 64, "invalid sha256");
    capi_checksum256 checksum;
    from_hex(hex_str, (char*)checksum.hash, sizeof(checksum.hash));
    return checksum;
}

// From capi_256 to string 
std::string to_hex(const char* d, uint32_t s) {
    std::string r;
    const char* to_hex = "0123456789abcdef";
    uint8_t* c = (uint8_t*)d;
    for (uint32_t i = 0; i < s; ++i)
        (r += to_hex[(c[i] >> 4)]) += to_hex[(c[i] & 0x0f)];
    return r;
}

std::string sha256_to_hex(const capi_checksum256& sha256) {
    return to_hex((char*)sha256.hash, sizeof(sha256.hash));
}

// Splits string with desired divider
// TODO: Try to make a function that doesn't uses stream
std::vector<std::string> split_string(std::string memo, char delimeter)
{
    std::stringstream ss(memo);
    std::string item;
    std::vector<std::string> splittedStrings;
    while (std::getline(ss, item, delimeter))
    {
       splittedStrings.push_back(item);
    }
    return splittedStrings;
}

// Parses memo to vector
// TODO: Try to make a function that doesn't uses stream
std::vector<std::string> parse_memo(std::string memo) 
{
    std::vector<std::string> memoContainers = split_string(memo, ',');
    std::vector<std::string> splittedStrings;
    for (std::vector<std::string>::iterator container = memoContainers.begin(); container != memoContainers.end(); ++container) {
        std::vector<std::string> itemTuple = split_string(*container, ':');
        for (std::vector<std::string>::iterator item = itemTuple.begin(); item != itemTuple.end(); ++item){
            splittedStrings.push_back(*item);
        }
    }
    return splittedStrings;
}

// Transfer percentage function
void transfer_percentage (eosio::name from, eosio::name to, eosio::asset quantity, std::string memo) {
    auto sym = quantity.symbol;
    auto amount = quantity.amount;
    std::string contract_name;
    
    if (sym == eosio::symbol("EOS", 4)){
        contract_name = "eosio.token";
    }
    else if (sym == eosio::symbol("GROW", 4)){
        contract_name = "groweostoken";
    }
    else {
        eosio_assert(false, "unknown asset");
    }

    //TODO -> Check why memo is not working as argument
    action(
        permission_level{from, "active"_n},
        name(contract_name),
        "transfer"_n,
        std::make_tuple(from, to, asset(amount,sym), std::string(""))
    ).send();
}

//----------------------------

/*
    Actions declaration
*/

/*
*   Action creategame
*   1. Ensure that the action has the signature of the host (this may change to come from a token contract)
*   2. Ensure that the game does not exist
*   2. Ensure that the host and challenged are not the same (maybe the challenged is the computer)
*   3. Check if the game already exists
*   3. Store the new created game in db
*/
void jopow_on_eos::creategame(  const eosio::name& challenged, 
                                const eosio::name& host, 
                                const eosio::asset& quantity, 
                                const std::string& hashed_move_str ){
    // check if authorized for account to create a game
    require_auth(_self);
    
    // check if host is not the same as challenged
    eosio_assert(challenged != host, "challenged shouldn't be the same as host");

    // This and more validations must be done in the token contract
    auto sym = quantity.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );

    // 1st param => The "code", which represents the contract's account. This value is accessible through the scoped _self variable.
    // 2nd param => The "scope" which defines the payer of the contract, the contract in this use case responsible for paying the ram fee.
    games games(_self, _self.value);
    
    //eosio_assert(itr == games.end(), "game already exists");

    // Add to table, first argument is account to bill for storage (change if needed)
    uint64_t id = games.available_primary_key();
    
    games.emplace(_self, [&]( auto& g ){
        g.id = id;
        g.host = host;
        g.challenged = challenged;
        g.quantity = quantity;
    });

    // Makes first move
    action(
        permission_level{_self, "active"_n},
        _self,
        "move"_n,
        //std::make_tuple(name(challenged), name(host), asset(1,symbol("EOS", 4)))
        std::make_tuple(id, name(host), hashed_move_str)
    ).send();

}

/*
*   Action acceptgame
*   1. Ensure that the action has the signature of the challenged user
*   2. Ensure that the game exists
*   3. Check if the game belongs to the account
*   4. Start the game, allowing a move to be made
*/
void jopow_on_eos::acceptgame(  const uint64_t& id, 
                                const eosio::name& challenged, 
                                const eosio::asset& quantity, 
                                const std::string& hashed_move_str){
    require_auth(_self);

    games games(_self, _self.value);
    
    auto itr = games.find(id);
    eosio_assert(itr != games.end(), "game doesn't exists!");

    eosio_assert(challenged != itr->host, "challenged shouldn't be the same as host");

    eosio_assert( itr->challenged == challenged || itr->challenged == name("none"), "this is not your gamme!" );

    eosio_assert(quantity == itr->quantity, "challenged transfer does not match game requirements");

    // Modify table, second argument is account to bill for storage (change if needed)
    games.modify( itr, _self, [&]( auto& g){
        g.challenged = challenged;
        g.start_game();
        g.can_reveal = true;
    });

    // Makes first move
    action(
        permission_level{_self, "active"_n},
        _self,
        "move"_n,
        //std::make_tuple(name(challenged), name(host), asset(1,symbol("EOS", 4)))
        std::make_tuple(id, name(challenged), hashed_move_str)
    ).send();
}

/*
*   Action close
*   1. Ensure that the action has the signature from the host
*   2. Ensure that the game exists
*   3. Remove the game from the db
*/
void jopow_on_eos::close(const uint64_t& id, const eosio::name& by){
    require_auth(by);

    games games(_self, _self.value);
    auto itr = games.find(id);

    eosio_assert(itr != games.end(), "game doesn't exist");
    eosio_assert(by == itr->host || by == itr->challenged, "only host or challenged can close this game");
    eosio_assert(itr->can_reveal == false, "you can't close this game, challenged already bet");

    auto sym = itr->quantity.symbol;
    auto amount = itr->quantity.amount;
    std::string memo = "Here is your bet money back! Play again";
    transfer_percentage(_self, itr->host, asset(amount,sym), memo);

    
    //Remove game
    games.erase(itr);
}

/*
*   Action move
*   1. Ensure that the action has the signature from the host/challenged
*   2. Ensure that the game exists
*   //3. Ensure tha the game was accepted by host and challenged (IMPORTANT)
*   4. Ensure that the game is not finished yet
*   5. Ensure that the move action is done by host/ challenged
*   6. Ensure that this is the right user's turn when necessary turns, if not just check if start is allowed(3)
*   7. Verify movement is valid
*   8. Update game with the new move
*   9. Change the move_turn to the other player if necessary
*   10. Determine if there is a winner
*   11. Store the updated game to the db
*   TODO: Handle draws and restart game, update scores and do best of three logic and other ones
*/
void jopow_on_eos::move(const uint64_t& id, const eosio::name& by, const std::string& hashed_move_str){
    //1
    require_auth(_self);
    //2
    // games games(_self, host);
    games games(_self, _self.value);
    
    auto itr = games.find(id);
    
    eosio_assert(itr != games.end(), "game doesn't exist!");
    
    //3
    //eosio_assert(itr->can_start == true, "The challenged didn't accept the game yet!" );
    //4
    eosio_assert(itr->winner == name("none") || itr->winner == name("draw"), "the game has ended!");
    //5
    eosio_assert(by == itr->host || by == itr->challenged, "this is not your game!");
    //6
    //eosio_assert(by == itr->turn, "it's not your turn yet!");
    //7
    //eosio_assert( is_valid_movement(move, itr->host_move, itr->challenged_move, by, host, challenged), "not a valid movement!");

    // Converts hex string to checksum256
    capi_checksum256 hashed_move256 = hex_to_sha256(hashed_move_str);

    //8, 9, 10
    games.modify(itr, _self, [&]( auto& g ) {
        g.challenged_move256 = (by == itr->challenged) ? hashed_move256 : itr->challenged_move256;
        g.host_move256 = (by == itr->host) ? hashed_move256 : itr->host_move256;
    });
}


void jopow_on_eos::reveal( const uint64_t& id, const std::string& seed_host, const std::string& seed_challenged ){
    //Only self can excute this action by now
    require_auth(_self);

    games games(_self, _self.value);
    auto itr = games.find(id);

    eosio_assert( itr->can_reveal == true, "game was not accepted");
    eosio_assert( itr->host_move == 0 || itr->challenged_move == 0, "game already finished");

    std::string host_move256_str =  sha256_to_hex(itr->host_move256);
    std::string challenged_move256_str =  sha256_to_hex(itr->challenged_move256);
    
    uint8_t host_move = 0;
    uint8_t challenged_move = 0;

    capi_checksum256 host_move256_calc;
    capi_checksum256 challenged_move256_calc;

    capi_checksum256 host_move256_seed = hex_to_sha256(std::string(seed_host) + std::string("1"));
    sha256((char *)&host_move256_seed, sizeof(host_move256_seed), &host_move256_calc);
    //printhex((const char *)&host_move256_calc, sizeof(host_move256_calc));
    if(sha256_to_hex(host_move256_calc) == host_move256_str){
        host_move = 1;
    } else {
        host_move256_seed = hex_to_sha256(std::string(seed_host) + std::string("2"));
        sha256((char *)&host_move256_seed, sizeof(host_move256_seed), &host_move256_calc);
        if(sha256_to_hex(host_move256_calc) == host_move256_str){
            host_move = 2;
        } else {
            host_move256_seed = hex_to_sha256(std::string(seed_host) + std::string("3"));
            sha256((char *)&host_move256_seed, sizeof(host_move256_seed), &host_move256_calc);
            if(sha256_to_hex(host_move256_calc) == host_move256_str){
                host_move = 3;
            } else {
                host_move = -1;
            }
        }
    }

    capi_checksum256 challenged_move256_seed = hex_to_sha256(std::string(seed_challenged) + std::string("1"));
    sha256((char *)&challenged_move256_seed, sizeof(challenged_move256_seed), &challenged_move256_calc);
    if(sha256_to_hex(challenged_move256_calc) == challenged_move256_str){
        challenged_move = 1;
    } else {
        challenged_move256_seed = hex_to_sha256(std::string(seed_challenged) + std::string("2"));
        sha256((char *)&challenged_move256_seed, sizeof(challenged_move256_seed), &challenged_move256_calc);
        if(sha256_to_hex(challenged_move256_calc) == challenged_move256_str){
            challenged_move = 2;
        } else {
            challenged_move256_seed = hex_to_sha256(std::string(seed_challenged) + std::string("3"));
            sha256((char *)&challenged_move256_seed, sizeof(challenged_move256_seed), &challenged_move256_calc);
            if(sha256_to_hex(challenged_move256_calc) == challenged_move256_str){
                challenged_move = 3;
            } else {
                challenged_move = -1;
            }
        }
    }

    //Updates table
    games.modify( itr, _self, [&]( auto& g){
        g.challenged_move = challenged_move;
        g.host_move = host_move;
        g.winner = get_winner(g);
    });

    if(itr->winner == itr->host){
        auto sym = itr->quantity.symbol;
        auto amount = 2 * (itr->quantity.amount);
        std::string memo = "Well done. You win";
        transfer_percentage(_self, itr->host, asset(amount,sym), memo);
    }
    else if(itr->winner == itr->challenged){
        auto sym = itr->quantity.symbol;
        auto amount = 2 * (itr->quantity.amount);
        std::string memo = "Well done. You win";
        transfer_percentage(_self, itr->challenged, asset(amount,sym), memo);
    }
    else if(itr->winner =="draw"_n){
        std::string memo = "There was a draw.";
        transfer_percentage(_self, itr->host, itr->quantity, memo);
        transfer_percentage(_self, itr->challenged, itr->quantity, memo);

    }
    else if(itr->winner == "error"_n){
        std::string memo = "An error ocurred";
        transfer_percentage(_self, itr->host,  itr->quantity, memo);
        transfer_percentage(_self, itr->challenged,  itr->quantity, memo);
    }
    else{
        eosio_assert(false, "error. unknown how to handle winner");
    }
}

void jopow_on_eos::ontransfer(  const name&            from,
                                const name&            to,
                                const asset&           quantity,
                                const std::string&     memo) 
{   
    games games(_self, _self.value);

    //std::vector<std::string> parsed_memo_itens = parse_memo("action:creategame,challenged:user2,host:user1,hashmove:....");
    //std::vector<std::string> parsed_memo_itens = parse_memo("action:acceptgame,id:0,challenged:user2,hashmove:....");

    std::vector<std::string> parsed_memo_itens = parse_memo(memo);

    auto sym = quantity.symbol;
    auto amount = quantity.amount;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( quantity.is_valid(), "invalid supply");
    eosio_assert( amount > 0, "max-supply must be positive");
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );
    
    if(parsed_memo_itens.size()){
        eosio_assert( parsed_memo_itens.size() >= 2, "invalid memo" );
        if(parsed_memo_itens[0] == "action"){
            if(parsed_memo_itens[1] == "creategame") {
                eosio_assert( parsed_memo_itens.size() == 8, "invalid creategame memo" );
                eosio_assert( parsed_memo_itens[2] == "challenged", "creategame challenged not found" );
                eosio_assert( parsed_memo_itens[4] == "host", "creategame host not found");
                eosio_assert( parsed_memo_itens[6] == "hashmove", "move hash not found");
                eosio_assert( name(parsed_memo_itens[5]) == from, "invalid host player");

                auto challenged = parsed_memo_itens[3];
                auto host = parsed_memo_itens[5];
                auto hashmove = parsed_memo_itens[7];

                action(
                    permission_level{_self, "active"_n},
                    _self,
                    "creategame"_n,
                    //std::make_tuple(name(challenged), name(host), asset(1,symbol("EOS", 4)))
                    std::make_tuple(name(challenged), name(host), asset(amount,sym), hashmove)
                ).send();
            }
            else if(parsed_memo_itens[1] == "acceptgame") {
                eosio_assert( parsed_memo_itens.size() == 8, "invalid acceptgame memo" );
                eosio_assert( parsed_memo_itens[2] == "id", "acceptgame id not found" );
                eosio_assert( parsed_memo_itens[4] == "challenged", "acceptgame challenged not found");
                eosio_assert( parsed_memo_itens[6] == "hashmove", "move hash not found");
                eosio_assert( name(parsed_memo_itens[5]) == from, "invalid challenged player");

                //TODO verify stoull 64 bits????
                uint64_t id = std::stoull(parsed_memo_itens[3]);
                auto challenged = parsed_memo_itens[5];
                auto hashmove = parsed_memo_itens[7];

                action(
                    permission_level{_self, "active"_n},
                    _self,
                    "acceptgame"_n,
                    std::make_tuple(id, name(challenged), asset(amount,sym), hashmove)
                ).send();
            }
            else {
                eosio_assert(false, "invalid action");
            }
        } else {
            // TODO verify transfer with empty memo, just a transfer, memo null
            //eosio_assert(false, "invalid memo");
        }
    }
}

//EOSIO_DISPATCH( jopow_on_eos, (creategame)(acceptgame)(move)(close)(reveal))

extern "C" {
    void apply(uint64_t receiver, uint64_t code, uint64_t action) {
        if(code == receiver && action == name("creategame").value) {
            execute_action(name(receiver), name(code), &jopow_on_eos::creategame);
        }
        else if(code == receiver && action == name("acceptgame").value) {
            execute_action(name(receiver), name(code), &jopow_on_eos::acceptgame);
        }
        else if(code == receiver && action == name("move").value) {
            execute_action(name(receiver), name(code), &jopow_on_eos::move);
        }
        else if(code == receiver && action == name("close").value){
            execute_action(name(receiver), name(code), &jopow_on_eos::close);
        }
        else if(code == receiver && action == name("reveal").value){
            execute_action(name(receiver), name(code), &jopow_on_eos::reveal);
        }
        //Verify necessity of validatin some error coming from eosio
        else if((code == name("eosio.token").value) && (action == name("transfer").value)) {
            execute_action(name(receiver), name(code), &jopow_on_eos::ontransfer);
        }
        else if((code == name("groweostoken").value) && (action == name("transfer").value)) {
            execute_action(name(receiver), name(code), &jopow_on_eos::ontransfer);
        }
    }
}
