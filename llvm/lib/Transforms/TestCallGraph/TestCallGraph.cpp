#include "TestCallGraph.h"

const llvm::Function* TestCallGraphWrapper::getFunctionInCallGrpahByName(std::string name){
    for(auto beg = cg->begin(), end = cg->end(); beg != end; ++beg){
        if(beg->first != nullptr && beg->first->getName() == name){
            return beg->first;
        }
    }
    return nullptr;
}

size_t TestCallGraphWrapper::countPointerLevel(const llvm::Instruction *inst){
    size_t pointerLevel = 1;

    auto ty = inst->getType();
    while(ty->getPointerElementType()->isPointerTy()){
        ++pointerLevel;
        ty = ty->getPointerElementType();
    }
    return pointerLevel;
}

void TestCallGraphWrapper::initialize(const llvm::Function * const func){
    for(auto &inst : llvm::instructions(*func)){
        if(const llvm::AllocaInst *pi = llvm::dyn_cast<llvm::AllocaInst>(&inst)){
            memoryLocationMap[pi] = llvm::MemoryLocation(&inst, llvm::LocationSize(func->getParent()->getDataLayout().getTypeAllocSize(pi->getType())));
            labelMap[pi].insert(Label(&inst, Label::LabelType::Def));
            size_t ptrLvl = countPointerLevel(pi);
            worklist[ptrLvl].insert(&inst);
        }
    }
}

void TestCallGraphWrapper::dumpWorkList(){
    // llvm::DenseMap<size_t, llvm::DenseSet<const llvm::Instruction*>> worklist;

    for(auto beg = worklist.begin(), end = worklist.end(); beg != end; ++beg){
        size_t ptrLevel = beg->first;
        llvm::DenseSet<const llvm::Instruction*> elements = beg->second;
        for(auto b = elements.begin(), e = elements.end(); b != e; ++b){
            llvm::errs() << *(*b) << "\n";
        }
    }

}


bool TestCallGraphWrapper::runOnModule(llvm::Module &m){

    getCallGraphFromModule(m);

    auto mainFunctionPtr = getFunctionInCallGrpahByName("main");
    if(mainFunctionPtr){
        llvm::errs() << "Found main function.\n" ;
    }
    else{
        llvm::errs() << "nullptr.\n" ;
    }

    performPointerAnalysisOnFunction(mainFunctionPtr);

    return false;
}


char TestCallGraphWrapper::ID = 0;
static llvm::RegisterPass<TestCallGraphWrapper> X("TestCallGraph", "TestCallGraph");

void TestCallGraphWrapper::performPointerAnalysisOnFunction(const llvm::Function * const func){
    if(visited.count(func)){
        return;
    }

    initialize(func);
    size_t currentPointerLevel = worklist.size();

    while(currentPointerLevel != 0){
        propagate(currentPointerLevel, func);
        --currentPointerLevel;
    }

    visited[func] = true;
}

void TestCallGraphWrapper::propagate(size_t currentPtrLvl, const llvm::Function* func){
    for(auto ptr : worklist[currentPtrLvl]){
        for(auto puser : ptr->users()){
            auto pi = llvm::dyn_cast<llvm::Instruction>(puser);
            if(auto *pinst = llvm::dyn_cast<llvm::StoreInst>(pi)){
                labelMap[pi].insert(Label(pinst->getPointerOperand(), Label::LabelType::Def));
                labelMap[pi].insert(Label(pinst->getPointerOperand(), Label::LabelType::Use));
                labelMap[pi].insert(Label(pinst->getValueOperand(), Label::LabelType::Alias));
                useList[pinst->getPointerOperand()].push_back(pi);
            }
            else if(auto *pinst = llvm::dyn_cast<llvm::LoadInst>(pi)){
                labelMap[pi].insert(Label(pinst->getPointerOperand(), Label::LabelType::Use));
                labelMap[pi].insert(Label(pi, Label::LabelType::AliasDefine));
                useList[pinst->getPointerOperand()].push_back(pi);
            }
            else{
                llvm::errs() << *pi << "is in the user list of pointer " << *ptr << ", but it's neither storeinst nor loadinst.\n";
            }
        }

        for(auto pi : useList[ptr]){
            auto pd = findDefFromUse(pi, ptr);
            for(auto def : pd){
                defUseGraph[def][pi].push_back(ptr);
            }
        }

        auto initialDUEdges = getDUEdgesOfPtrAtClause(defUseGraph[ptr], ptr);
        std::vector<std::tuple<const llvm::Instruction*, const llvm::Instruction*, const llvm::Instruction*>> propagateList;
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
            llvm::errs() << "Current: " << *f << " ===== " << *pvar << " ====> " << *t << "\n";
            // todo: but we also neeo to consider if we need to make it an assignment to some points to result due to information passed along a single edge.
            // todo: one problem is that in the case 3->1 along path A and 3->2 along path 2 and path A is then updated to 3->4, the result 
            //      is wrong by {1,2,4} instead of {2,4}.
            propagatePointsToInformation(t, f, pvar);
            if(auto pt = llvm::dyn_cast<llvm::StoreInst>(t)){
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
            else if(auto pt = llvm::dyn_cast<llvm::LoadInst>(t)){
                auto tmp = aliasMap[t][t];
                updateAliasInformation(t,pt);
                if(tmp != aliasMap[t][t]){
                    for(auto user0 : t->users()){
                        llvm::errs() << "Handling user " << *user0 << "\n";
                        auto user = llvm::dyn_cast<Instruction>(user0);
                        // todo: we need another list for the case
                        /*
                            y = load x
                            z = load y
                            store z a

                            Here, the aliasMap for z should be propagate to the store clause, but currenly it is not propagated. 
                        */
                        if(auto pt0 = llvm::dyn_cast<llvm::StoreInst>(user)){
                            if(t == pt0->getPointerOperand()){
                                aliasMap[user][t] = aliasMap[t][t];

                                if(auto pins = llvm::dyn_cast<Instruction>(pt0->getValueOperand())){
                                    llvm::errs() << *user << " " << *(pt0->getValueOperand()) << " " << *t << "\n";
                                    llvm::errs() << aliasMap[user][pt0->getValueOperand()] << "\n";
                                    llvm::errs() << "ttttt: " << aliasMap[user][t] << "\n";
                                    if(aliasMap[user][pt0->getValueOperand()].empty()){
                                        pointsToSet[user][t] = std::set<const llvm::Value*>{pt0->getValueOperand()};
                                        for(auto tt0 : aliasMap[user][t]){
                                            auto tt = llvm::dyn_cast<Instruction>(tt0);
                                            pointsToSet[user][tt] = pointsToSet[user][t];
                                        }
                                    }
                                    else{
                                        pts[user][t] = aliasMap[user][pt0->getValueOperand()];
                                        for(auto tt0 : aliasMap[user][t]){
                                            auto tt = llvm::dyn_cast<Instruction>(tt0);
                                            pts[user][tt] = pts[user][t];
                                        }
                                    }
                                    // pts[user][t] = getAlias(user, dyn_cast<Instruction>(pt0->getValueOperand()));
                                }
                                else{
                                    llvm::errs() << "FUCK\n";
                                    pointsToSet[user][t] = std::set<const llvm::Value*>{pt0->getValueOperand()};
                                }

                                llvm::errs() << "Pts ttttt: " << pointsToSet[user][t] << "\n";


                                for(auto tt : aliasMap[user][t]){
                                    labelMap[user].insert(Label(tt, Label::LabelType::Def));
                                    labelMap[user].insert(Label(tt, Label::LabelType::Use));
                                }
                                
                            }
                            else if(t == pt0->getValueOperand()){
                                aliasMap[user][t] = aliasMap[t][t];
                                // todo: are we only need ptspointsto(user,t) or also need ptspointsto(user, aliasmap[user][t])
                                auto ptrChangeList = ptsPointsTo(user,t);
                                llvm::errs() << "changeList: " << ptrChangeList << "\n";
                                for(auto p0 : ptrChangeList){
                                    // todo: create a function that updates the pts set and return a set of propagation edges.
                                    auto p1 = llvm::dyn_cast<Instruction>(p0);
                                    pointsToSet[user][p1] = aliasMap[user][t];
                                }
                                // todo: after changing pts set, we need to propagate the change along the def use edge.
                            }
                            else{
                                llvm::errs() << "Hitting at " << *pt0 << " with pointer " << *t << "\n";
                            }
                        }
                        else if(auto pt0 = llvm::dyn_cast<llvm::LoadInst>(user)){
                            aliasMap[user][t] = aliasMap[t][t];
                            for(auto tt : aliasMap[user][t]){
                                labelMap[user].insert(Label(tt, Label::LabelType::Use));
                                useList[tt].push_back(user);
                            }

                        }
                        else{
                            llvm::errs() << "Wrong clause type: " << *user << "\n";
                        }

                        // dumpDebugInfo();
                    }
                }
            }
            // dumpDebugInfo();
        }




    }
}

