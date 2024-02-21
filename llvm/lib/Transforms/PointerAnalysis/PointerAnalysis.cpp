#include "PointerAnalysis.h"

// TODO: move the includes into header.
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <set>
#include <algorithm>
#include <tuple>
#include <stdexcept>
#include <queue>
#include <iostream>


#define RED_BOLD_PREFIX "\033[1;31m"
#define GREEN_BOLD_PREFIX "\033[1;32m"
#define YELLOW_BOLD_PREFIX "\033[1;33m"
#define ALL_RESET "\033[0m"


using namespace llvm;


llvm::raw_ostream& operator<<(llvm::raw_ostream &os, const Label &l){
    if(l.type == Label::LabelType::None){
        os << "None";
    }
    else if(l.type == Label::LabelType::Def){
        os << "Def(" << *l.p << ")";
    }
    else if(l.type == Label::LabelType::Use){
        os << "Use(" << *l.p << ")";
    }
    else if(l.type == Label::LabelType::DefUse){
        os << "DefUse(" << *l.p << ")";
    }
    return os;
}

llvm::raw_ostream& operator<<(llvm::raw_ostream &os, const std::vector<std::tuple<const Instruction*, const Instruction*, PointerAnalysisWrapper::PointerType>> &v){
    os << "Propagate List\n";
    
    for(auto &tup : v){
        auto f = std::get<0>(tup);
        auto t = std::get<1>(tup);
        auto pvar = std::get<2>(tup);
        os << *f << " ===== " << *pvar << " ====> " << *t << "\n";
    }
    return os; 
}

llvm::raw_ostream& operator<<(llvm::raw_ostream &os, const std::set<Label> &v){
    for(auto vv : v){
        os << vv << " ";
    }
    return os;
}

llvm::raw_ostream& operator<<(llvm::raw_ostream &os, const std::set<std::string> &v){
    for(auto vv : v){
        os << vv << " ";
    }
    return os;
}

llvm::raw_ostream& operator<<(llvm::raw_ostream &os, const std::vector<std::string> &v){
    for(auto vv : v){
        os << vv << " ";
    }
    return os;
}

// llvm::raw_ostream& operator<<(llvm::raw_ostream &os, const std::vector<std::shared_ptr<MemoryObject>> &v){
//     for(auto vv : v){
//         os << vv->getName() << " ";
//     }
//     return os;
// }

llvm::raw_ostream& operator<<(llvm::raw_ostream &os, const std::set<const Value*> &v){
    // os << v.begin()->getType();
    for(auto vv : v){
        // os << vv->getType() << "\n";
        if(vv){
            os << *vv << " ";
        }
        else{
            os << "Unknown" << " ";
        }
    }
    return os;
}

llvm::raw_ostream& operator<<(llvm::raw_ostream &os, const std::set<const Instruction*> &v){
    os << v.size() << "\n";
    for(auto vv : v){
        // os << typeid(vv).name() << "\n";
        os << "\t\t" << *vv << " ";
    }
    return os;
}

llvm::raw_ostream& operator<<(llvm::raw_ostream &os, const std::vector<const Value*> &v){
    os << "PassList" << "\n";
    for(auto vv : v){
        // os << typeid(vv).name() << "\n";
        os << "\t\t" << *vv << " ";
    }
    return os;
}

/*
    Given a pointer, return the the pointer level. The pointer level is defined as the number of '*' used
    in defining the pointer.
*/
size_t PointerAnalysisWrapper::countPointerLevel(const Instruction *inst){
    size_t pointerLevel = 1;

    auto ty = inst->getType();
    while(ty->getPointerElementType()->isPointerTy()){
        ++pointerLevel;
        ty = ty->getPointerElementType();
    }
    return pointerLevel;
}



bool PointerAnalysisWrapper::runOnFunction(llvm::Function &F){

    // Initialize worklist first.
    initialize(F);

    size_t currentPointerLevel = worklist.size();

    while(currentPointerLevel != 0){
        propagate(currentPointerLevel, F);
        --currentPointerLevel;
    }

    // dumpPointerMap();
    // dumpAliasMap();

    // we are not modifying the ir.
    return false;

}


/*
    Initialize the worklist based on all allocation instructions (%1 = alloca <type>, align <int>).
    This function count the pointer level and put variables according to the level.
    We use the whole instruction to represents the LHS variable (%1 in this case) since LLVM IR use partial SSA form. Besides, storing LHS
    variable only is generally hard since %1 is only a temporary name and inst.getName() returns an emoty string.
*/
void PointerAnalysisWrapper::initialize(Function &F){

    for(auto &inst : instructions(F)){
        /*
            The reason why we cannot use inst.users() to find the def-use chain is the users() will give wrong information on pointers.
            e.g., 
                1. b = alloca <type>
                2. store a, b // load a into b
                3. c = load b

                1's users will be 2 and 3 instead of only 2.

        */

        if(AllocaInst *pi = dyn_cast<AllocaInst>(&inst)){
            pts[&inst][&inst] = std::set<const Value*>{nullptr};
            inst2label[&inst].insert(Label(&inst, Label::LabelType::Def));
            size_t ptrLvl = countPointerLevel(pi);
            worklist[ptrLvl].push_back(&inst);
        }
    }

}

void PointerAnalysisWrapper::propagate(size_t currentPtrLvl, Function &F){
    for(auto ptr : worklist[currentPtrLvl]){
        for(auto puser : ptr->users()){
            auto pi = dyn_cast<Instruction>(puser);
            if(auto *pinst = dyn_cast<StoreInst>(pi)){
                inst2label[pi].insert(Label(pinst->getPointerOperand(), Label::LabelType::Def));
                inst2label[pi].insert(Label(pinst->getPointerOperand(), Label::LabelType::Use));
                inst2label[pi].insert(Label(pinst->getValueOperand(), Label::LabelType::Alias));
                useList[pinst->getPointerOperand()].push_back(pi);
            }
            else if(auto *pinst = dyn_cast<LoadInst>(pi)){
                inst2label[pi].insert(Label(pinst->getPointerOperand(), Label::LabelType::Use));
                inst2label[pi].insert(Label(pi, Label::LabelType::AliasDefine));
                useList[pinst->getPointerOperand()].push_back(pi);
            }
            else{
                errs() << *pi << "is in the user list of pointer " << *ptr << ", but it's neither storeinst nor loadinst.\n";
            }
        }

        for(auto pi : useList[ptr]){
            auto pd = findDefFromUse(pi, ptr);
            for(auto def : pd){
                defUseGraph[def][pi].push_back(ptr);
            }
        }

        auto initialDUEdges = getDUEdgesOfPtrAtClause(defUseGraph[ptr], ptr);
        std::vector<std::tuple<const Instruction*, const Instruction*, PointerType>> propagateList;
        for(auto pu : initialDUEdges){
            propagateList.push_back(std::make_tuple(ptr, pu, ptr));
        }
        while(!propagateList.empty()){
            auto tup = propagateList.front();
            propagateList.erase(propagateList.begin());
            // errs() << propagateList;
            auto f = std::get<0>(tup);
            auto t = std::get<1>(tup);
            auto pvar = std::get<2>(tup);
            errs() << "Current: " << *f << " ===== " << *pvar << " ====> " << *t << "\n";
            // todo: but we also neeo to consider if we need to make it an assignment to some points to result due to information passed along a single edge.
            // todo: one problem is that in the case 3->1 along path A and 3->2 along path 2 and path A is then updated to 3->4, the result 
            //      is wrong by {1,2,4} instead of {2,4}.
            propagatePointsToInformation(t, f, pvar);
            if(auto pt = dyn_cast<StoreInst>(t)){
                auto tmp = pts[t][pvar];
                // todo: update all vector to set
                calculatePointsToInformationForStoreInst(t, pvar, pt);
                if(tmp != pts[t][pvar]){
                    auto passList = getDUEdgesOfPtrAtClause(defUseGraph[t], pvar);
                    // errs() << passList << "\n";
                    for(auto u : passList){
                        propagateList.push_back(std::make_tuple(t,u,pvar));
                    }
                }
            }
            else if(auto pt = dyn_cast<LoadInst>(t)){
                auto tmp = aliasMap[t][t];
                updateAliasInformation(t,pt);
                if(tmp != aliasMap[t][t]){
                    for(auto user0 : t->users()){
                        errs() << "Handling user " << *user0 << "\n";
                        auto user = dyn_cast<Instruction>(user0);
                        // todo: we need another list for the case
                        /*
                            y = load x
                            z = load y
                            store z a

                            Here, the aliasMap for z should be propagate to the store clause, but currenly it is not propagated. 
                        */
                        if(auto pt0 = dyn_cast<StoreInst>(user)){
                            if(t == pt0->getPointerOperand()){
                                aliasMap[user][t] = aliasMap[t][t];

                                if(auto pins = dyn_cast<Instruction>(pt0->getValueOperand())){
                                    errs() << "FUCK1\n" << *user << " " << *(pt0->getValueOperand()) << " " << *t << "\n";
                                    errs() << aliasMap[user][pt0->getValueOperand()] << "\n";
                                    errs() << "ttttt: " << aliasMap[user][t] << "\n";
                                    if(aliasMap[user][pt0->getValueOperand()].empty()){
                                        pts[user][t] = std::set<const Value*>{pt0->getValueOperand()};
                                        for(auto tt0 : aliasMap[user][t]){
                                            auto tt = dyn_cast<Instruction>(tt0);
                                            pts[user][tt] = pts[user][t];
                                        }
                                    }
                                    else{
                                        pts[user][t] = aliasMap[user][pt0->getValueOperand()];
                                        for(auto tt0 : aliasMap[user][t]){
                                            auto tt = dyn_cast<Instruction>(tt0);
                                            pts[user][tt] = pts[user][t];
                                        }
                                    }
                                    // pts[user][t] = getAlias(user, dyn_cast<Instruction>(pt0->getValueOperand()));
                                }
                                else{
                                    errs() << "FUCK\n";
                                    pts[user][t] = std::set<const Value*>{pt0->getValueOperand()};
                                }

                                errs() << "Pts ttttt: " << pts[user][t] << "\n";


                                for(auto tt : aliasMap[user][t]){
                                    inst2label[user].insert(Label(tt, Label::LabelType::Def));
                                    inst2label[user].insert(Label(tt, Label::LabelType::Use));
                                }
                                
                            }
                            else if(t == pt0->getValueOperand()){
                                aliasMap[user][t] = aliasMap[t][t];
                                // todo: are we only need ptspointsto(user,t) or also need ptspointsto(user, aliasmap[user][t])
                                auto ptrChangeList = ptsPointsTo(user,t);
                                errs() << "changeList: " << ptrChangeList << "\n";
                                for(auto p0 : ptrChangeList){
                                    // todo: create a function that updates the pts set and return a set of propagation edges.
                                    auto p1 = dyn_cast<Instruction>(p0);
                                    pts[user][p1] = aliasMap[user][t];
                                }
                                // todo: after changing pts set, we need to propagate the change along the def use edge.
                            }
                            else{
                                errs() << "Hitting at " << *pt0 << " with pointer " << *t << "\n";
                            }
                        }
                        else if(auto pt0 = dyn_cast<LoadInst>(user)){
                            aliasMap[user][t] = aliasMap[t][t];
                            for(auto tt : aliasMap[user][t]){
                                inst2label[user].insert(Label(tt, Label::LabelType::Use));
                                useList[tt].push_back(user);
                            }

                        }
                        else{
                            errs() << "Wrong clause type: " << *user << "\n";
                        }

                        dumpDebugInfo();
                    }
                }
            }
            dumpDebugInfo();
        }




    }
}


std::vector<std::tuple<const Instruction*, const Instruction*, PointerAnalysisWrapper::PointerType>> 
    PointerAnalysisWrapper::updatePointsToSetAndPropagate(const Instruction *i0, PointerType p0, const Instruction *i1, PointerType p1){

    std::vector<std::tuple<const Instruction*, const Instruction*, PointerType>> res;

    if(pts[i0][p0] != aliasMap[i1][p1]){
        auto passList = getDUEdgesOfPtrAtClause(defUseGraph[i0], p0);
        for(auto u : passList){
            res.push_back(std::make_tuple(i0,u,p0));
        }
        return res;
    }
    return std::vector<std::tuple<const Instruction*, const Instruction*, PointerType>>();
}


std::set<const llvm::Value*> PointerAnalysisWrapper::getAlias(const Instruction *t, PointerType p){
    
    if(auto pt = dyn_cast<StoreInst>(p)){
        std::set<const llvm::Value*> pointees = aliasMap[t][pt->getValueOperand()];
        if(pointees.empty()){
            return std::set<const llvm::Value*>{dyn_cast<Instruction>(pt->getValueOperand())};
        }
        return pointees;
    }
    else if(auto pt = dyn_cast<LoadInst>(p)){
        std::set<const llvm::Value*> pointees = aliasMap[t][pt->getPointerOperand()];
        if(pointees.empty()){
            return std::set<const llvm::Value*>{dyn_cast<Instruction>(pt->getPointerOperand())};
        }
        return pointees;
    }
    
}

void PointerAnalysisWrapper::updateAliasInformation(const llvm::Instruction *t, PointerType pt){
    auto aliases = getAlias(t, pt);

    for(auto &b : aliases){
        auto a = dyn_cast<Instruction>(b);
        aliasMap[t][t].insert(pts[t][a].begin(), pts[t][a].end());
    }
    
    errs() << aliasMap[t][t] << "\n";

    return;
}


void PointerAnalysisWrapper::calculatePointsToInformationForStoreInst(const Instruction *t, PointerType pvar, const StoreInst *pt){
    // todo: if there is no entry for aliasMap[t][pt->getValueOperand()], we need 
    //          to check the aliasMap[pt->getValueOperand()][pt->getValueOperand()], if still empty, then the pointer is just alias to itself.

    std::set<const llvm::Value*> pointees = aliasMap[t][pt->getValueOperand()];
    if(pointees.empty()){
        pts[t][pvar] = std::set<const Value*>{dyn_cast<Instruction>(pt->getValueOperand())};
    }
    else{
        pts[t][pvar] = pointees;
    }
    
    return;
}

void PointerAnalysisWrapper::propagatePointsToInformation(const Instruction *t, const Instruction *f, PointerType pvar){
    pts[t][pvar].insert(pts[f][pvar].begin(), pts[f][pvar].end());
    return;
}



/*
    Collects pointers that points to "t" as instruction "user".
*/
std::vector<const llvm::Value*> PointerAnalysisWrapper::ptsPointsTo(const Instruction *user, PointerType t){
    std::vector<const llvm::Value*> res;

    auto candidatePointers = pts[user];
    for(auto iter = candidatePointers.begin(); iter != candidatePointers.end(); ++iter){
        auto pointsToSet = iter->second;
        auto it = std::find_if(pointsToSet.begin(), pointsToSet.end(), [&](const Value *pvar) -> bool {return pvar == t;});
        if(it != pointsToSet.end()){
            res.push_back(iter->first);
        }
    }

    return res;
}


std::vector<const Instruction*> PointerAnalysisWrapper::findDefFromUse(const Instruction *ptr, PointerType pi){
    while(true){
        auto tmp = ptr->getPrevNonDebugInstruction();
        if(tmp){
            if(hasDef(tmp, pi)){
                return std::vector<const Instruction*>{tmp};
            }
            ptr = tmp;
        }
        else{
            std::vector<const Instruction*> res;
            for(auto it = pred_begin(ptr->getParent()); it != pred_end(ptr->getParent()); ++it){
                const BasicBlock *pbb = *it;
                std::vector<const Instruction*> defs = findDefFromBB(pbb, pi);
                res.insert(res.end(), defs.begin(), defs.end());
            }
            return res;
        }
    }
}


bool PointerAnalysisWrapper::hasDef(const Instruction *pinst, PointerType p){
    auto labels = inst2label[pinst];
    auto iter = std::find_if(labels.begin(), labels.end(), [&](Label l) -> bool {return l.type == Label::LabelType::Def && l.p == p;});
    if(iter != labels.end()){
        return true;
    }
    else{
        return false;
    }
}

std::vector<const Instruction*> PointerAnalysisWrapper::findDefFromBB(const BasicBlock *pbb, PointerType p){
    auto lastInst = &(pbb->back());
    while(lastInst){
        if(hasDef(lastInst, p)){
            return std::vector<const Instruction*>{lastInst};
        }
        lastInst = lastInst->getPrevNonDebugInstruction();
    }
    std::vector<const Instruction*> res;
    for(auto it = pred_begin(pbb); it != pred_end(pbb); ++it){
        const BasicBlock *pbb = *it;
        std::vector<const Instruction*> defs = findDefFromBB(pbb, p);
        res.insert(res.end(), defs.begin(), defs.end());
    }
    return res;
}

std::vector<const Instruction*> 
    PointerAnalysisWrapper::getDUEdgesOfPtrAtClause(std::map<const Instruction*, std::vector<PointerType>> u2p, PointerType ptr){
        std::vector<const Instruction*> res;
        for(auto iter = u2p.begin(); iter != u2p.end(); ++iter){
            for(auto it = iter->second.begin(); it != iter->second.end(); ++it){
                if(*it == ptr){
                    res.push_back(iter->first);
                }
            }
        }
            
        return res;
    }










void PointerAnalysisWrapper::dumpWorklist() const{
    #define DEBUG_STR_LENGTH 30
    errs() << "\n" << std::string(DEBUG_STR_LENGTH, '*') << RED_BOLD_PREFIX "Worklist" ALL_RESET << std::string(DEBUG_STR_LENGTH, '*') << "\n";
    for(auto it = worklist.begin(); it != worklist.end(); it = worklist.upper_bound(it->first)){
        errs() << "Pointer level " << it->first << ":\n";
        for(auto var : it->second){
            errs() <<"\t" << *var << "\n";
        }
    }
    errs() << std::string(DEBUG_STR_LENGTH * 2, '*') << "\n\n";
    #undef DEBUG_STR_LENGTH

}

void PointerAnalysisWrapper::dumpInst2Label() const{
    #define DEBUG_STR_LENGTH 30
    errs() << "\n" << std::string(DEBUG_STR_LENGTH, '*') << RED_BOLD_PREFIX "inst2label" ALL_RESET << std::string(DEBUG_STR_LENGTH, '*') << "\n";
    for(auto it = inst2label.begin(); it != inst2label.end(); ++it){
        errs() << *(it->first) << GREEN_BOLD_PREFIX "\t==>\t" ALL_RESET << it->second << "\n";
    }
    errs() << std::string(DEBUG_STR_LENGTH * 2, '*') << "\n\n";
    #undef DEBUG_STR_LENGTH

}

void PointerAnalysisWrapper::dumpPointerMap() const{
    #define DEBUG_STR_LENGTH 30
    errs() << "\n" << std::string(DEBUG_STR_LENGTH, '*') << RED_BOLD_PREFIX "pointerMap" ALL_RESET << std::string(DEBUG_STR_LENGTH, '*') << "\n";
    for(auto it = pts.begin(); it != pts.end(); ++it){
        errs() << "At instruction: " YELLOW_BOLD_PREFIX << *(it->first) << ALL_RESET ":\n";
        for(auto iter = it->second.begin(); iter != it->second.end(); ++iter){
            errs() << GREEN_BOLD_PREFIX "\t";  
            errs() << *(iter->first) << "\n\t\t";
            errs() << iter->second << "\n";
            errs() << ALL_RESET "\n";
        }
    }
    errs() << std::string(DEBUG_STR_LENGTH * 2, '*') << "\n\n";
    #undef DEBUG_STR_LENGTH

}


void PointerAnalysisWrapper::dumpRelated() const{
    // #define DEBUG_STR_LENGTH 30
    // errs() << "\n" << std::string(DEBUG_STR_LENGTH, '*') << RED_BOLD_PREFIX "related" ALL_RESET << std::string(DEBUG_STR_LENGTH, '*') << "\n";
    // for(auto it = related.begin(); it != related.end(); ++it){
    //     errs() << "Variable: " YELLOW_BOLD_PREFIX << *(it->first) << ALL_RESET " is related to " GREEN_BOLD_PREFIX << it->second << ALL_RESET "\n";
    // }
    // errs() << std::string(DEBUG_STR_LENGTH * 2, '*') << "\n\n";
    // #undef DEBUG_STR_LENGTH

}



/*
    A debug function that outputs all data structures.
    May choose to comment out the function before deveriling the code.
*/
void PointerAnalysisWrapper::debug() const{

    dumpWorklist();
    dumpPointerMap();
    dumpRelated();



    // print inst2label
    // dumpInst2Label();



    // print Var2MO
    // dumpVar2MO();



}




std::vector<Instruction *> PointerAnalysisWrapper::getRelatedClause(PointerType var){

    std::vector<Instruction *> v = related[var];
    return v;
}
















void PointerAnalysisWrapper::markDefAndUse(PointerType pointer){
    // DEBUGMSG(*pointer);
    // for (auto iter : pointer->users()){
    //     DEBUGMSG(*iter);
    // }
    inst2label[pointer].insert(Label(pointer, Label::LabelType::Def));
    for(auto clause : related[pointer]){
        if(isa<StoreInst>(clause)){
            inst2label[clause].insert(Label(pointer, Label::LabelType::DefUse));
        }
        else if(isa<LoadInst>(clause)){
            inst2label[clause].insert(Label(pointer, Label::LabelType::Use));
        }
    }
}

void PointerAnalysisWrapper::buildDefUseGraph(llvm::Function &F){

    for(auto ptr : worklist[currentWorkingPointerLevel]){
        markDefAndUse(ptr);
    }


    std::map<PointerType, const Instruction*> defDict;
    auto &firstBB = F.getEntryBlock();
    std::queue<BasicBlock*> bbQueue;
    bbQueue.push(&firstBB);

    while(!bbQueue.empty()){
        auto bb = bbQueue.front();
        bbQueue.pop();
        auto nextbbs = findDefUsePerBasicBlock(*bb, defDict);
        for(auto nextbb : nextbbs){
            bbQueue.push(nextbb);
        }
    }

    --currentWorkingPointerLevel;
    return;
}

std::vector<BasicBlock*> PointerAnalysisWrapper::findDefUsePerBasicBlock(const BasicBlock &bb, std::map<PointerType, const Instruction*> &defDict){
    for(auto &inst : bb){
        errs() << inst << "\n";
        if(inst.isTerminator()){
            std::vector<BasicBlock*> nextbbs;
            auto numSuccessors = inst.getNumSuccessors();
            for(size_t sz = 0; sz != numSuccessors; ++sz){
                nextbbs.push_back(inst.getSuccessor(sz));
            }
            return nextbbs;
        }

        // First handling used pointers is important since we do not want to introduce a self dependence for a store clause. 
        std::vector<PointerType> usedPointers = getUsedPointers(inst);
        for(auto ptr : usedPointers){
            errs() << "finding: " << *ptr << "\n";
            if(defDict.find(ptr) == defDict.end()){
                std::terminate();
                // std::string str = "";
                // raw_string_ostream ss(str);
                // ss << *ptr;
                // throw std::runtime_error("Pointer: " + str + " used before defined.");
            }
            createDefUseEdge(defDict[ptr], &inst, ptr);
        }

        std::vector<PointerType> definedPointers = getDefinedPointers(inst);
        for(auto ptr : definedPointers){
            defDict[ptr] = &inst;
        }

        
    }
    std::terminate();
    // throw std::runtime_error("No terminator in current basicblock.");

}

std::vector<PointerAnalysisWrapper::PointerType> PointerAnalysisWrapper::getDefinedPointers(const Instruction &inst){
    std::vector<PointerType> res;
    for(auto label : inst2label[&inst]){
        if(label.type == Label::LabelType::Def || label.type == Label::LabelType::DefUse){
            res.push_back(dyn_cast<Instruction>(label.p));
        }
    }
    return res;
}

std::vector<PointerAnalysisWrapper::PointerType> PointerAnalysisWrapper::getUsedPointers(const Instruction &inst){
    std::vector<PointerType> res;
    for(auto label : inst2label[&inst]){
        if(label.type == Label::LabelType::Use || label.type == Label::LabelType::DefUse){
            res.push_back(dyn_cast<Instruction>(label.p));
        }
    }
    return res;
}

void PointerAnalysisWrapper::createDefUseEdge(const Instruction* def, const Instruction* use, PointerType ptr){
    defUseGraph[def][use].push_back(ptr);
    return;
}



void PointerAnalysisWrapper::dumpDefUseGraph() const{
    #define DEBUG_STR_LENGTH 30
    errs() << "\n" << std::string(DEBUG_STR_LENGTH, '*') << RED_BOLD_PREFIX "def use graph" ALL_RESET << std::string(DEBUG_STR_LENGTH, '*') << "\n";
    for(auto it = defUseGraph.begin(); it != defUseGraph.end(); ++it){
        errs() << YELLOW_BOLD_PREFIX << *(it->first) << ALL_RESET " =======> " GREEN_BOLD_PREFIX << ALL_RESET << "\n";
    }
    errs() << std::string(DEBUG_STR_LENGTH * 2, '*') << "\n\n";
    #undef DEBUG_STR_LENGTH
}

void PointerAnalysisWrapper::dumpAliasMap() const{
    #define DEBUG_STR_LENGTH 30
    errs() << "\n" << std::string(DEBUG_STR_LENGTH, '*') << RED_BOLD_PREFIX "Alias Map" ALL_RESET << std::string(DEBUG_STR_LENGTH, '*') << "\n";
    for(auto it = aliasMap.begin(); it != aliasMap.end(); ++it){
        errs() << YELLOW_BOLD_PREFIX << *(it->first) << ALL_RESET " =======> " << "\n";
        for(auto it0 = it->second.begin(); it0 != it->second.end(); ++it0){
            errs() << GREEN_BOLD_PREFIX << *(it0->first) << ALL_RESET " =======> " << it0->second << "\n";
        }
    }
    errs() << std::string(DEBUG_STR_LENGTH * 2, '*') << "\n\n";
    #undef DEBUG_STR_LENGTH
}

void PointerAnalysisWrapper::dumpDebugInfo() const{
    dumpPointerMap();
    dumpAliasMap();

    return;
}


bool operator<(const Label &l1, const Label &l2){
    if(l1.type == l2.type){
        return l1.p < l2.p;
    }
    else{
        return l1.type < l2.type;
    }
}












void PointerAnalysisWrapper::getAnalysisUsage(AnalysisUsage &AU) const{
    AU.setPreservesAll();
}

char PointerAnalysisWrapper::ID = 0;

static RegisterPass<PointerAnalysisWrapper> X("intra-pointer-analysis", "An intra-procedural pointer analysis.");

