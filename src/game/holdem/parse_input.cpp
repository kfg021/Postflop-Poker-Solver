#include "game/holdem/parse_input.hpp"

#include "game/game_types.hpp"
#include "game/game_utils.hpp"

#include <string>
#include <vector>

Result<CardSet> buildCommunityCardsFromVector(const std::vector<std::string>& communityCardsVector) {
    CardSet communityCards = 0;
    for (const auto& cardString : communityCardsVector) {
        Result<CardID> cardIDResult = getCardIDFromName(cardString);
        if (cardIDResult.isError()) {
            return cardIDResult.getError();
        }

        const CardID& cardID = cardIDResult.getValue();
        if (setContainsCard(communityCards, cardID)) {
            return "Error building community cards: \"" + cardString + "\" appears more than once.";
        }

        communityCards |= cardIDToSet(cardID);
    }

    int communityCardSize = getSetSize(communityCards);
    if (communityCardSize < 3 || communityCardSize > 5) {
        return "Error building community cards: Size must be 3, 4, or 5 (flop, turn, or river).";
    }

    return communityCards;
}

Result<std::vector<Holdem::RangeElement>> buildRangeFromVector(const std::vector<std::string>& rangeVector) {
    assert(false);
    return "Not implemented yet!";
}
