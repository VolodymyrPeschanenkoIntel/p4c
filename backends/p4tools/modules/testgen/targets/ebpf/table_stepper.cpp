#include "backends/p4tools/modules/testgen/targets/ebpf/table_stepper.h"

#include <map>
#include <vector>

#include <boost/format.hpp>
#include <boost/none.hpp>
#include <boost/optional/optional.hpp>

#include "lib/error.h"

#include "backends/p4tools/modules/testgen/core/small_step/table_stepper.h"
#include "backends/p4tools/modules/testgen/lib/execution_state.h"
#include "backends/p4tools/modules/testgen/lib/test_spec.h"
#include "backends/p4tools/modules/testgen/targets/ebpf/expr_stepper.h"

namespace P4Tools {

namespace P4Testgen {

namespace EBPF {

const IR::Expression *EBPFTableStepper::computeTargetMatchType(
    ExecutionState *nextState, const KeyProperties &keyProperties,
    std::map<cstring, const FieldMatch> *matches, const IR::Expression *hitCondition) {
    // If the custom match type does not match, delete to the core match types.
    return TableStepper::computeTargetMatchType(nextState, keyProperties, matches, hitCondition);
}

void EBPFTableStepper::checkTargetProperties(
    const std::vector<const IR::ActionListElement *> & /*tableActionList*/) {
    // Iterate over the table keys and check whether we can mitigate taint.
    for (auto keyProperties : properties.resolvedKeys) {
        const auto *keyElement = keyProperties.key;
        auto keyIsTainted =
            (keyProperties.isTainted &&
             (properties.tableIsImmutable || keyElement->matchType->toString() == "exact"));
        properties.tableIsTainted = properties.tableIsTainted || keyIsTainted;
        // If the key expression is tainted, do not bother resolving the remaining keys.
        if (properties.tableIsTainted) {
            ::warning("Key %1% of table %2% is tainted.", keyElement->expression, table);
            return;
        }
    }
}

void EBPFTableStepper::evalTargetTable(
    const std::vector<const IR::ActionListElement *> &tableActionList) {
    const auto *keys = table->getKey();
    // If we have no keys, there is nothing to match.
    if (keys == nullptr) {
        addDefaultAction(boost::none);
        return;
    }

    // If the table is not constant, the default action can always be executed.
    // This is because we can simply not enter any table entry.
    boost::optional<const IR::Expression *> tableMissCondition = boost::none;

    // If the table is not immutable, we synthesize control plane entries and follow the paths.
    if (properties.tableIsImmutable) {
        EBPFProperties.implementaton = TableImplementation::constant;
    }

    switch (EBPFProperties.implementaton) {
        case TableImplementation::skip: {
            break;
        };
        case TableImplementation::constant: {
            // If the entries properties is constant it means the entries are fixed.
            // We cannot add or remove table entries.
            tableMissCondition = evalTableConstEntries();
            break;
        };
        default: {
            evalTableControlEntries(tableActionList);
        }
    }

    // Add the default action.
    addDefaultAction(tableMissCondition);
}

EBPFTableStepper::EBPFTableStepper(EBPFExprStepper *stepper, const IR::P4Table *table)
    : TableStepper(stepper, table) {}

}  // namespace EBPF

}  // namespace P4Testgen

}  // namespace P4Tools
