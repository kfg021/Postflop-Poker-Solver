#include "io/output.hpp"

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

using json = nlohmann::ordered_json;

json buildJSON(const IGameRules& rules, const Node& node, const Tree& tree);

json buildJSONChance(const IGameRules& rules, const ChanceNode& chanceNode, const Tree& tree) {
    json j;
    j["Node Type"] = "Chance";
    // TODO: Finish chance nodes
    return j;
}

json buildJSONDecision(const IGameRules& rules, const DecisionNode& decisionNode, const Tree& tree) {
    json j;
    j["Node Type"] = "Decision";
    j["Player"] = getPlayerID(decisionNode.player);

    j["Valid Actions"] = {};
    for(int i = 0; i < decisionNode.decisionDataSize; ++i) {
        ActionID actionID = tree.allDecisions[decisionNode.decisionDataOffset + i];
        j["Valid Actions"].push_back(rules.getActionName(actionID));
    }

    auto& strategy = j["Strategy"];
    for (int i = 0; i < decisionNode.numTrainingDataSets; ++i) {
        auto averageStrategy = getAverageStrategy(decisionNode, tree, i);
        for (int j = 0; j < decisionNode.decisionDataSize; ++j) {
            ActionID actionID = tree.allDecisions[decisionNode.decisionDataOffset + j];    
            strategy[rules.getHandName(i)][rules.getActionName(actionID)] = averageStrategy[j];
        }
    }


    auto& children = j["Children"];
    for (int i = 0; i < decisionNode.decisionDataSize; ++i) {
        ActionID actionID = tree.allDecisions[decisionNode.decisionDataOffset + i];
        std::size_t nextNodeIndex = tree.allDecisionNextNodeIndices[decisionNode.decisionDataOffset + i];
        assert(nextNodeIndex < tree.allNodes.size());
        children[rules.getActionName(actionID)] = buildJSON(rules, tree.allNodes[nextNodeIndex], tree);
    }

    return j;
}

json buildJSONFold(const FoldNode& foldNode) {
    json j;
    j["Node Type"] = "Fold";
    j["Winning Player"] = getPlayerID(foldNode.remainingPlayer);
    j["Reward"] = foldNode.remainingPlayerReward;
    return j;
}

json buildJSONShowdown(const ShowdownNode& showdownNode) {
    json j;
    j["Node Type"] = "Showdown";
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

void outputStrategyToJSON(const IGameRules& rules, const Tree& tree, const std::string& filePath) {
    std::ofstream file(filePath);
    assert(file.is_open());

    json j = buildJSON(rules, tree.getRootNode(), tree);
    file << j.dump(4) << std::endl;
}