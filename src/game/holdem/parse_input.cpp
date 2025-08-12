// Result<CardSet> buildCommunityCardsFromString(const std::string& communityCardsString) {
//     CardSet communityCards = 0;

//     std::istringstream stream(communityCardsString);
//     std::string cardString;
//     while (stream >> cardString) {
//         CardID cardID = getCardIDFromName(cardString);
//         if (!setContainsCard(communityCards, cardID)) {
//             return "Duplicate card in community card string.";
//         }
//         communityCards |= cardIDToSet(cardID);
//     }

//     // Only flop, turn or river solves are supported
//     int boardSize = getSetSize(communityCards);
//     if (boardSize < 3 || boardSize > 5) {
//         return "Incorrect number of cards provided (3, 4, or 5 is required).";
//     }

//     return communityCards;
// }

// Result<Holdem> Holdem::buildHoldem(const Holdem::GameSettings& settings) {
//     Result<CardSet> communityCardsResult = buildCommunityCardsFromString(settings.startingCommunityCards);
//     if (communityCardsResult.isError()) {
//         return "Error parsing community cards: " + communityCardsResult.getError();
//     }

//     Result<std::vector<RangeElement>> player0RangeResult = buildSortedRangeFromString(settings.oopRange);
//     if (player0RangeResult.isError()) {
//         return "Error parsing OOP player range: " + player0RangeResult.getError();
//     }

//     Result<std::vector<RangeElement>> player1RangeResult = buildSortedRangeFromString(settings.ipRange);
//     if (player1RangeResult.isError()) {
//         return "Error parsing IP player range: " + player1RangeResult.getError();
//     }

//     for (std::int32_t betSize : settings.betSizes) {
//         if (betSize <= 0) {
//             return "Invalid bet size"
//         }
//     }

//     InternalGameSettings internalSettings = {
//         .startingCommunityCards = communityCardsResult.getValue(),
//         .playerRanges = { player0RangeResult.getValue(), player1RangeResult.getValue() },
//         .betSizes = settings.betSizes,
//         .raiseSizes = settings.raiseSizes,
//     };
//     return Holdem(internalSettings);
// }

// std::vector<RangeElement> buildSortedRangeFromString(const std::string& rangeString) {
//     std::vector<RangeElement> rangeVector;


//     std::istringstream stream(rangeString);
//     std::string rangeElementString;
//     while (stream >> rangeElementString) {
//         assert(rangeElementString.size() >= 2);

//         Value value0 = getValueFromChar(rangeElementString[0]);
//         Value value1 = getValueFromChar(rangeElementString[1]);

//         if (value0 < value1) std::swap(value0, value1);

//         enum class Combos : std::uint8_t {
//             DEFAULT,
//             SUITED,
//             OFFSUIT,
//         };

//         Combos combos = Combos::DEFAULT;
//         if (rangeString.size() >= 3) {
//             switch (rangeString[2]) {
//                 case 's':
//                     combos = Combos::SUITED;
//                     break;
//                 case 'o':
//                     combos = Combos::OFFSUIT;
//                     break;
//                 default:
//                     assert(rangeString[2] == ':');
//                     break;
//             }
//         }

//         bool isPocketPair = (value0 == value1);
//         if (isPocketPair) {
//             assert(combos == Combos::DEFAULT);
//         }

//         std::uint8_t frequency = 100;
//         std::size_t colonLocation = rangeString.find(':');
//         if (colonLocation != std::string::npos) {
//             int freq = std::stoi(rangeString.substr(colonLocation + 1));
//             assert(freq > 0 && freq <= 100);
//             frequency = static_cast<std::uint8_t>(freq);
//         }

//         for (int suit0 = 0; suit0 < 4; ++suit0) {
//             for (int suit1 = 0; suit1 < 4; ++suit1) {
//                 if (isPocketPair && (suit0 <= suit1)) continue;
//                 if ((combos == Combos::OFFSUIT) && (suit0 == suit1)) continue;
//                 if ((combos == Combos::SUITED) && (suit0 != suit1)) continue;

//                 CardID card0 = getCardIDFromValueAndSuit(value0, static_cast<Suit>(suit0));
//                 CardID card1 = getCardIDFromValueAndSuit(value1, static_cast<Suit>(suit1));

//                 CardSet hand = cardIDToSet(card0) | cardIDToSet(card1);

//                 assert(ra.find(hand) == range.end());

//                 range.emplace(hand, frequency);
//             }
//         }

//         return rangeVector;
//     }
// }