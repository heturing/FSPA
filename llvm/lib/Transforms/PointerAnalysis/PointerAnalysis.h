#ifndef LLVM_FLOW_SENSITIVE_POINTER_ANALYSIS_H
#define LLVM_FLOW_SENSITIVE_POINTER_ANALYSIS_H

#include <string>
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Type.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Constants.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/MemoryLocation.h"
#include <map>
#include <vector>
#include <queue>
#include <memory>
#include <set>

#define POINTERANALYSISDEBUG

#ifdef POINTERANALYSISDEBUG
#define DEBUGMSG(s) do {errs() << s << "\n";} while(false)
#else
#define DEBUGMSG(s) do {} while(false)
#endif




class Label;



// Wrapper class for intra-procedural pointer analysis.
class PointerAnalysisWrapper : public llvm::FunctionPass{
    friend class llvm::Instruction;
    public:
        // llvm uses partial SSA form, thus each alloca instruction can uniquely identify a pointer.
        using PointerType = const llvm::Instruction*;
        using MemoryObjectType = llvm::MemoryLocation;

        
        using PointsToType = std::map<PointerType, std::set<std::shared_ptr<MemoryObjectType>>>;

    private:
        // Manage which memory objects a pointer can point to.
        PointsToType pointerMap;

        std::map<const llvm::Instruction*, std::map<const PointerType, std::set<const llvm::Value*>>> pts;

        std::map<size_t, std::vector<PointerType>> worklist;
        // Manage which instructions a pointer is related to.
        // Specifically, for inst "store x y", we say this inst is related to ptr if ptr == x.
        // For "x = load y", we say this inst is related to ptr if ptr == y.
        std::map<PointerType, std::vector<llvm::Instruction *>> related;



        inline size_t countPointerLevel(const llvm::Instruction *inst);


        void initialize(llvm::Function &F);
        void debug() const;
        // MemoryObjectType getMemoryObject(PointerType inst);

        void dumpRelated() const;
        // void dumpVar2MO() const;
        void dumpPointerMap() const;
        void dumpInst2Label() const;
        void dumpWorklist() const;

      




        
        
        // ====> working here.
        void propagate(size_t currentPtrLvl, llvm::Function &F);
        std::vector<const llvm::Value*> ptsPointsTo(const llvm::Instruction *user, PointerType t);
        std::vector<const llvm::Instruction*> findDefFromUse(const llvm::Instruction *ptr, PointerType pi);
        bool hasDef(const llvm::Instruction *pinst, PointerType p);
        std::vector<const llvm::Instruction*> findDefFromBB(const llvm::BasicBlock *pbb, PointerType p); 
        std::vector<const llvm::Instruction*> 
            getDUEdgesOfPtrAtClause(std::map<const llvm::Instruction*, std::vector<PointerType>> u2p, PointerType ptr);

        void propagatePointsToInformation(const llvm::Instruction *t, const llvm::Instruction *f, PointerType pvar);
        void calculatePointsToInformationForStoreInst(const llvm::Instruction *t, PointerType pvar, const llvm::StoreInst *pt);
        void updateAliasInformation(const llvm::Instruction *t, PointerType pt);
        std::vector<std::tuple<const llvm::Instruction*, const llvm::Instruction*, PointerType>> 
            updatePointsToSetAndPropagate(const llvm::Instruction *i0, PointerType p0, const llvm::Instruction *i1, PointerType p1);

        std::set<const llvm::Value*> getAlias(const llvm::Instruction *t, PointerType p);


        void markDefAndUse(PointerType pointer);
        void buildDefUseGraph(llvm::Function &F);
        std::vector<llvm::BasicBlock*> findDefUsePerBasicBlock(const llvm::BasicBlock &bb, std::map<PointerType, const llvm::Instruction*> &defDict);
        std::vector<PointerType> getDefinedPointers(const llvm::Instruction &inst);
        std::vector<PointerType> getUsedPointers(const llvm::Instruction &inst);
        void createDefUseEdge(const llvm::Instruction* def, const llvm::Instruction* use, PointerType ptr);
        size_t currentWorkingPointerLevel;
        void dumpDefUseGraph() const;
        void dumpAliasMap() const;
        void dumpDebugInfo() const;

        
        
        // => have not been fixed.
        
        // map a variable name to a memory objects that used for representing an abstract memory location.
        std::map<PointerType, std::set<MemoryObjectType>> Var2MO;
        std::map<const llvm::Instruction*, std::set<Label>> inst2label;
        std::map<const llvm::Instruction*, std::map<const llvm::Instruction*, std::vector<PointerType>>> defUseGraph;
        std::map<const llvm::Instruction*, std::map<const llvm::Value*, std::set<const llvm::Value *>>> aliasMap;
        std::map<const llvm::Value*, std::vector<const llvm::Instruction*>> useList;
        
        
        
        
        




        // std::set<MemoryObject::addressType> getPointsToSet(llvm::Instruction *at, std::shared_ptr<MemoryObject> mo);
        // std::set<std::string> getMOsFromVar(llvm::Instruction *at, PointerType pvar);

        std::vector<llvm::Instruction *> getRelatedClause(PointerType var);
        // Get the pointer of the definition instruction of a use(mo) at inst.
        // std::vector<llvm::Instruction*> getDef(llvm::Instruction *inst, MemoryObject::addressType mo);
        // llvm::Instruction* findDefInBB(llvm::BasicBlock*, MemoryObject::addressType, llvm::Instruction *at);

        // void createDefUseEdge(llvm::Instruction *def, llvm::Instruction *use, MemoryObject::addressType addr);
        // void propagate(std::queue<std::tuple<llvm::Instruction*, llvm::Instruction*, MemoryObject::addressType>> newEdges);
        // void buildDefUseGraphAndPropagate();

        // bool strongUpdate(llvm::Instruction *use, MemoryObject::addressType addr);
        // bool weakUpdate(llvm::Instruction *def, llvm::Instruction *use, MemoryObject::addressType addr);


        // MemoryObject::addressType createAddressForConstantFP(PointerType inst);
        // MemoryObject::addressType createAddressForConstantInt(PointerType inst);
        // MemoryObject::addressType getConstantAddr(llvm::ConstantData *val);
        // std::shared_ptr<MemoryObject> findMOAtInstruction(llvm::Instruction *use, std::shared_ptr<MemoryObject> mo);
        // MemoryObject::addressType getArgumentAddr(llvm::Argument *val);
        // std::set<MemoryObject::addressType> getMemoryObjectNameAtInstruction(llvm::Instruction *inst, PointerType var);
        // std::set<std::shared_ptr<MemoryObject>> getMemoryObjectFromAddress(llvm::Instruction *inst, MemoryObject::addressType addr);


    public:
        static char ID;
        PointerAnalysisWrapper() : FunctionPass(ID) {}

        PointsToType getPointerMap() {return pointerMap;}

        bool runOnFunction(llvm::Function &F) override;

        void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;
    
};



// Since we need to create labels before creating def-use edge, we need to associate an instruction to a series of labels.
// This class represents a single label. As a label, it records:
//      1. whether this is a def or use or def-use.
//      2. the memoryobject being defed or used. 
struct Label{

    const llvm::Value *p;
    enum class LabelType{
        None = 0, Use, Def, DefUse, Alias, AliasDefine
    };
    LabelType type;

    // Label() = default;
    Label(const llvm::Value *p, Label::LabelType t) : p(p), type(t) {}
};

llvm::raw_ostream& operator<<(llvm::raw_ostream &os, const Label &l);
bool operator<(const Label &l1, const Label &l2);


#endif