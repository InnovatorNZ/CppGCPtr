#ifndef CPPGCPTR_PHASEENUM_H
#define CPPGCPTR_PHASEENUM_H

#include <string>

enum class MarkState {
    REMAPPED,
    M0,
    M1
};

enum class eGCPhase {
    NONE,
    MARK_M0,
    MARK_M1,
    SWEEP
};

class MarkStateUtil {
public:
    static MarkState flipState(MarkState state) {
        switch (state) {
            case MarkState::M0:
                return MarkState::M1;
            case MarkState::M1:
                return MarkState::M0;
            case MarkState::REMAPPED:
                return MarkState::REMAPPED;
        }
    }

    static std::string toString(MarkState state) {
        switch (state) {
            case MarkState::REMAPPED:
                return "Remapped";
            case MarkState::M0:
                return "M0";
            case MarkState::M1:
                return "M1";
            default:
                return "Invalid";
        }
    }

    static std::string toString(eGCPhase state) {
        switch (state) {
            case eGCPhase::NONE:
                return "Not GC";
            case eGCPhase::MARK_M0:
                return "Marking (M0)";
            case eGCPhase::MARK_M1:
                return "Marking (M1)";
            case eGCPhase::SWEEP:
                return "Sweeping";
            default:
                return "Invalid";
        }
    }
};

#endif //CPPGCPTR_PHASEENUM_H