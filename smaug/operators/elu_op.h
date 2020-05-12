#ifndef _OPERATORS_ELU_OP_H_
#define _OPERATORS_ELU_OP_H_

#include <string>

#include "smaug/core/backend.h"
#include "smaug/operators/unary_op.h"

namespace smaug {

template <typename Backend>
class EluOp : public UnaryOp<Backend> {
   public:
    EluOp(const std::string& name, Workspace* workspace, float _alpha = 0.1)
            : UnaryOp<Backend>(name, OpType::ELU, workspace), alpha(_alpha) {}

    void run() override {}
    std::string opTypeName() const override { return "ELU"; }

    void setAlpha(float _alpha) { alpha = _alpha; }
    float getAlpha() const { return alpha; }

   protected:
    float alpha;
};

template <typename Backend>
class SeluOp : public EluOp<Backend> {
   public:
    SeluOp(const std::string& name, Workspace* workspace)
            : EluOp<Backend>(name, workspace, 1.6733), lambda(1.0507) {
        this->opType = OpType::SELU;
    }

    void run() override {}
    std::string opTypeName() const override { return "SELU"; }

    void setLambda(float _lambda) { lambda = _lambda; }
    float getLambda() const { return lambda; }

   protected:
    float lambda;
};

REGISTER_SPECIAL_OP(EluOp, ReferenceBackend);
REGISTER_SPECIAL_OP(SeluOp, ReferenceBackend);

}  // namespace smaug

#endif
