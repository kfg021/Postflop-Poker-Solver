#include "trainer/output.hpp"

#include "game/game_rules.hpp"
#include "game/game_types.hpp"
#include "game/game_utils.hpp"
#include "solver/tree.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

namespace {
using json = nlohmann::ordered_json;

json buildJSON(const IGameRules& rules, const Node& node, const Tree& tree);

json buildJSONChance(const IGameRules& rules, const ChanceNode& chanceNode, const Tree& tree) {
    json j;
    j["NodeType"] = "Chance";

    auto& nextCards = j["NextCards"];
    for (int i = 0; i < chanceNode.chanceDataSize; ++i) {
        CardID cardID = tree.allChanceCards[chanceNode.chanceDataOffset + i];
        nextCards.push_back(getNameFromCardID(cardID));
    }

    auto& children = j["Children"];
    for (int i = 0; i < chanceNode.chanceDataSize; ++i) {
        std::size_t nextNodeIndex = tree.allChanceNextNodeIndices[chanceNode.chanceDataOffset + i];
        assert(nextNodeIndex < tree.allNodes.size());
        children.push_back(buildJSON(rules, tree.allNodes[nextNodeIndex], tree));
    }

    return j;
}

json buildJSONDecision(const IGameRules& rules, const DecisionNode& decisionNode, const Tree& tree) {
    json j;
    j["NodeType"] = "Decision";
    j["Player"] = getPlayerID(decisionNode.player);

    auto& validActions = j["ValidActions"];
    for (int i = 0; i < decisionNode.decisionDataSize; ++i) {
        ActionID actionID = tree.allDecisions[decisionNode.decisionDataOffset + i];
        assert(rules.getActionType(actionID) == ActionType::Decision);
        validActions.push_back(rules.getActionName(actionID));
    }

    auto& strategy = j["Strategy"];
    for (int i = 0; i < decisionNode.numTrainingDataSets; ++i) {
        auto averageStrategy = getAverageStrategy(decisionNode, tree, i);
        CardSet hand = rules.mapIndexToHand(decisionNode.player, i);
        std::string handName;
        for (std::string cardName : getCardSetNames(hand)) {
            handName += cardName;
        }

        for (float finalStrategy : averageStrategy) {
            strategy[handName].push_back(finalStrategy);
        }
    }

    auto& children = j["Children"];
    for (int i = 0; i < decisionNode.decisionDataSize; ++i) {
        std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + i];
        assert(nextNodeIndex < tree.allNodes.size());
        children.push_back(buildJSON(rules, tree.allNodes[nextNodeIndex], tree));
    }

    return j;
}

json buildJSONFold(const FoldNode& foldNode) {
    json j;
    j["NodeType"] = "Fold";
    j["WinningPlayer"] = getPlayerID(foldNode.remainingPlayer);
    j["Reward"] = foldNode.remainingPlayerReward;
    return j;
}

json buildJSONShowdown(const ShowdownNode& showdownNode) {
    json j;
    j["NodeType"] = "Showdown";
    j["Reward"] = showdownNode.reward;
    return j;
}

json buildJSON(const IGameRules& rules, const Node& node, const Tree& tree) {
    switch (node.nodeType) {
        case NodeType::Chance:
            return buildJSONChance(rules, node.chanceNode, tree);
        case NodeType::Decision:
            return buildJSONDecision(rules, node.decisionNode, tree);
        case NodeType::Fold:
            return buildJSONFold(node.foldNode);
        case NodeType::Showdown:
            return buildJSONShowdown(node.showdownNode);
        default:
            assert(false);
            return json{};
    }
}
} // namespace

void outputStrategyToJSON(const IGameRules& rules, const Tree& tree, const std::string& filePath) {
    std::ofstream file(filePath);
    assert(file.is_open());

    json j = buildJSON(rules, tree.getRootNode(), tree);
    file << j.dump(4) << std::endl;
}