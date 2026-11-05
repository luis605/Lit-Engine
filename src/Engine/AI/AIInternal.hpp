#pragma once

namespace AiDetail {

enum class Kind { Sequence, Selector, Parallel, Inverter, Succeeder, Failer, Repeat, Wait, Condition, Set, Action, Utility, Fsm };

enum class CombineMode { Product, Min, Max, Average, Sum };

enum class CompareOp { None, Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual };

struct Condition {
    enum class Type { Leaf, All, Any, Not, Always };
    Type type = Type::Always;
    std::string key;
    CompareOp op = CompareOp::None;
    Json value;
    std::string valueKey;
    std::vector<Condition> kids;
};

struct Consideration {
    std::string input;
    float minimum = 0.0f;
    float maximum = 1.0f;
    ParamCurve curve;
};

struct Option {
    std::string name;
    int child = -1;
    float weight = 1.0f;
    std::vector<Consideration> considerations;
    bool hasGate = false;
    Condition gate;
};

struct Transition {
    std::string to;
    Condition when;
    float after = -1.0f;
};

struct State {
    std::string name;
    int child = -1;
    std::vector<Transition> transitions;
};

struct Node {
    Kind kind = Kind::Sequence;
    std::string name;
    std::vector<int> children;
    std::string key;
    std::string action;
    Json value;
    std::string valueKey;
    bool add = false;
    int count = -1;
    float seconds = 0.0f;
    bool reactive = false;
    bool parallelAny = false;
    Condition condition;
    std::vector<Option> options;
    bool weighted = false;
    float power = 1.0f;
    float stickiness = 0.0f;
    float minScore = 0.0f;
    bool reconsider = true;
    CombineMode combine = CombineMode::Product;
    std::vector<State> states;
    std::vector<Transition> global;
    std::string initial;
    int subtreeEnd = 0;
};

struct NodeState {
    int index = 0;
    int last = -1;
    int counter = 0;
    float timer = 0.0f;
    float stateTime = 0.0f;
};

double toNumber(const Json& json);
std::string formatNumber(double value);
std::string conditionText(const Condition& condition);
bool evaluateCondition(const Condition& condition, const Blackboard& blackboard);

}

struct BehaviorGraph::Impl {
    std::string name;
    std::vector<AiDetail::Node> nodes;
    Json initial = Json::object();
    int root = -1;
};

struct BehaviorRunner::Impl {
    std::vector<AiDetail::NodeState> states;
    std::vector<int> currentState;
    std::vector<int> option;
    std::vector<uint8_t> running;
    std::vector<const ActionFn*> actions;
    std::vector<double> scores;
    std::vector<double> weights;
    std::vector<uint8_t> available;
};

namespace AiDetail {

struct Exec {
    const BehaviorGraph::Impl& graph;
    BehaviorRunner::Impl& runtime;
    const ActionRegistry& actions;
    Blackboard& blackboard;
    DecisionTrace& trace;
    Pcg32& rng;
    float dt;
    uint64_t tick;
    uint32_t agent;

    void resetSubtree(int id);
    void emit(const Node& node, const char* kind, std::string_view winner, std::string_view reason, std::vector<TraceOption> options = {});
    Status run(int id);
    Status exec(int id);

    Status execComposite(const Node& node, NodeState& state);
    Status execParallel(const Node& node);
    Status execDecorator(const Node& node, NodeState& state);
    void execSet(const Node& node);
    Status execAction(const Node& node);

    bool transitionFires(const Transition& transition, const NodeState& state) const;
    const Transition* firstFiredTransition(const Node& node, const NodeState& state, int current) const;
    Status fsm(int id);

    double score(const Node& node, const Option& option, bool& available, TraceOption* traceOption) const;
    void scoreOptions(const Node& node, int current, std::vector<TraceOption>* records);
    int pickWeighted(const Node& node, int best, std::string* reason);
    std::string describeBest(const Node& node, int best, int current) const;
    Status utility(int id);
};

}
