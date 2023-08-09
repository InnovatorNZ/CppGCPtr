#ifndef CPPGCPTR_PHASEENUM_H
#define CPPGCPTR_PHASEENUM_H

#include <string>
#include <stdexcept>

enum class MarkState {
    REMAPPED,
    M0,
    M1
};

enum class MarkStateBit {
    NOT_ALLOCATED,
    REMAPPED,
    M0,
    M1
};

enum class eGCPhase {
    NONE,
    CONCURRENT_MARK,
    REMARK,
    SWEEP
};

class MarkStateUtil {
public:
    static MarkState switchState(MarkState state) {
        switch (state) {
            case MarkState::M0:
                return MarkState::M1;
            case MarkState::M1:
                return MarkState::M0;
            case MarkState::REMAPPED:
                return MarkState::M0;
            default:
                throw std::exception();
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

    static MarkStateBit toMarkState(unsigned char state) {
        switch (state) {
            case 0:
                return MarkStateBit::NOT_ALLOCATED;
            case 1:
                return MarkStateBit::REMAPPED;
            case 2:
                return MarkStateBit::M0;
            case 3:
                return MarkStateBit::M1;
            default:
                throw std::invalid_argument("Invalid argument converting from char to MarkStateBit");
        }
    }

    static unsigned char toChar(MarkStateBit state) {
        switch (state) {
            case MarkStateBit::NOT_ALLOCATED:
                return 0;
            case MarkStateBit::REMAPPED:
                return 1;
            case MarkStateBit::M0:
                return 2;
            case MarkStateBit::M1:
                return 3;
            default:
                return 0;
        }
    }

    static const char* toString(MarkStateBit state) {
        switch (state) {
            case MarkStateBit::NOT_ALLOCATED:
                return "NOT_ALLOCATED";
            case MarkStateBit::REMAPPED:
                return "REMAPPED";
            case MarkStateBit::M0:
                return "M0";
            case MarkStateBit::M1:
                return "M1";
            default:
                return "???";
        }
    }
};

#endif //CPPGCPTR_PHASEENUM_H