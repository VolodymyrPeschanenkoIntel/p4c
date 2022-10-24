#ifndef COMMON_COMPILER_REACHABILITY_H_
#define COMMON_COMPILER_REACHABILITY_H_

#include <list>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

#include "frontends/common/resolveReferences/referenceMap.h"
#include "frontends/p4/callGraph.h"
#include "frontends/p4/typeMap.h"
#include "gsl/gsl-lite.hpp"
#include "ir/ir.h"
#include "ir/node.h"

namespace P4Tools {

using DCGVertexType = const IR::Node;
using DCGVertexTypeSet = std::unordered_set<const DCGVertexType*>;
using ReachabilityHashType = std::unordered_map<cstring, std::unordered_set<const DCGVertexType*>>;

template <class T>
class ExtendedCallGraph : public P4::CallGraph<T> {
    friend class P4ProgramDCGCreator;
    ReachabilityHashType hash;

 public:
    explicit ExtendedCallGraph(cstring name) : P4::CallGraph<T>(name) {}
    const ReachabilityHashType& getHash() const { return hash; }
    /// Function adds current vertex to a hash which allows to get access
    /// for vertexes from string in DCG.
    /// If name is empty then it doesn't hash it.
    void addToHash(T vertex, IR::ID name) {
        if (name.name.size() == 0) {
            return;
        }
        auto i = hash.find(name.name);
        if (i == hash.end()) {
            std::unordered_set<const DCGVertexType*> s;
            s.insert(vertex);
            hash.emplace(name.name, s);
        } else {
            i->second.insert(vertex);
        }
        const auto* prev = name.name.findlast('.');
        if (prev != nullptr) {
            cstring newName = name.name.before(prev);
            i = hash.find(newName);
            if (i == hash.end()) {
                addToHash(vertex, (newName.size() ? IR::ID(newName) : IR::ID()));
            }
        }
    }

    bool isReachable(T start, T element) const {
        CHECK_NULL(start);
        CHECK_NULL(element);
        std::set<T> work;
        std::set<T> visited;
        work.emplace(start);
        while (!work.empty()) {
            auto* node = *work.begin();
            if (node->equiv(*element)) {
                return true;
            }
            work.erase(node);
            if (visited.find(node) != visited.end()) {
                continue;
            }
            visited.emplace(node);
            auto edges = this->out_edges.find(node);
            if (edges == this->out_edges.end()) {
                continue;
            }
            for (auto c : *(edges->second)) {
                work.emplace(c);
            }
        }
        return false;
    }
};

using NodesCallGraph = ExtendedCallGraph<DCGVertexType*>;

/// The main class for building control flow DCG.
class P4ProgramDCGCreator : public Inspector {
    NodesCallGraph* dcg;
    DCGVertexTypeSet prev;
    P4::TypeMap* typeMap;
    P4::ReferenceMap* refMap;
    std::unordered_set<const DCGVertexType*> visited;
    const IR::P4Program* p4program;

 public:
    P4ProgramDCGCreator(P4::ReferenceMap* refMap, P4::TypeMap* typeMap, NodesCallGraph* dcg);

    bool preorder(const IR::Annotation* annotation) override;
    bool preorder(const IR::ConstructorCallExpression* callExpr) override;
    bool preorder(const IR::MethodCallExpression* method) override;
    bool preorder(const IR::MethodCallStatement* method) override;
    bool preorder(const IR::P4Action* action) override;
    bool preorder(const IR::P4Parser* parser) override;
    bool preorder(const IR::P4Table* table) override;
    bool preorder(const IR::ParserState* parserState) override;
    bool preorder(const IR::P4Control* control) override;
    bool preorder(const IR::P4Program* program) override;
    bool preorder(const IR::P4ValueSet* valueSet) override;
    bool preorder(const IR::SelectExpression* selectExpression) override;
    bool preorder(const IR::IfStatement* ifStatement) override;
    bool preorder(const IR::SwitchStatement* switchStatement) override;
    bool preorder(const IR::StatOrDecl* statOrDecl) override;

 protected:
    /// Function add edge to current @vertex in DCG.
    /// The edge is a pair (@prev, @vertex).
    void addEdge(const DCGVertexType* vertex, IR::ID vertexName = IR::ID());
};

/// The main data for reachability engine.
class ReachabilityEngineState {
    friend class ReachabilityEngine;
    const DCGVertexType* prevNode = nullptr;
    std::list<const DCGVertexType*> state;

 public:
    static ReachabilityEngineState* getInitial();
    ReachabilityEngineState* copy();
    std::list<const DCGVertexType*> getState();
};

using ReachabilityResult = std::pair<bool, const IR::Expression*>;

/// The main class to support user input patterns.
/// This class allows to move step-by-step via user's input expression.
/// Syntax for the user's input expressions
/// <behavior> ::= <behavior> ; <behavior> |
///                <behavior> + <behavior> |
///                <name> | <forbidden name>
/// <name> ::= <p4c node name> ['(' <condition> ')']
/// <forbidden name> ::= ! <p4c node name>
/// <p4c node name> - the name of the MethodCallStatement, P4Action, P4Parser,
///                   P4Table, ParserState, P4Control, ParserValueSet, Declaration.
/// <p4c condition> - any conditions p4c in syntax, whcih should be returned by the
//                    Engine if corresponded <p4c node name> was reached.
class ReachabilityEngine {
    gsl::not_null<const NodesCallGraph*> dcg;
    const ReachabilityHashType& hash;
    std::unordered_map<const DCGVertexType*, std::list<const DCGVertexType*>> userTransitions;
    std::unordered_map<const DCGVertexType*, const IR::Expression*> conditions;
    std::unordered_set<const DCGVertexType*> forbiddenVertexes;

 public:
    ReachabilityEngine(gsl::not_null<const NodesCallGraph*> dcg, std::string reachabilityExpression,
                       bool eliminateAnnotations = false);
    ReachabilityResult next(ReachabilityEngineState*, const DCGVertexType*);
    gsl::not_null<const NodesCallGraph*> getDCG();

 protected:
    void annotationToStatements(const DCGVertexType* node,
                                std::unordered_set<const DCGVertexType*>& s);
    void addTransition(const DCGVertexType*, const std::unordered_set<const DCGVertexType*>&);
    std::unordered_set<const DCGVertexType*> getName(std::string name);
    const IR::Expression* getCondition(const DCGVertexType*);
    const IR::Expression* addCondition(const IR::Expression* prev,
                                       const DCGVertexType* currentState);
    static const IR::Expression* stringToNode(std::string name);

 protected:
    /// Function add edge to current @vertex in DCG.
    /// The edge is a pair (@prev, @vertex).
    void addEdge(const DCGVertexType* vertex, IR::ID vertexName = IR::ID());
};

}  // namespace P4Tools

#endif /* COMMON_COMPILER_REACHABILITY_H_ */
