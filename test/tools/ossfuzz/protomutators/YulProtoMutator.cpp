#include <test/tools/ossfuzz/protomutators/YulProtoMutator.h>

#include <libyul/Exceptions.h>

#include <src/text_format.h>

using namespace solidity::yul::test::yul_fuzzer;

using namespace protobuf_mutator;

using namespace std;

template <typename Proto>
using YPR = YulProtoCBRegistration<Proto>;
using YPM = YulProtoMutator;
using PrintChanges = YPM::PrintChanges;

MutationInfo::MutationInfo(ProtobufMessage* _message, string const& _info):
	ScopeGuard([&]{ exitInfo(); }), m_protobufMsg(_message)
{
	print("----------------------------------");
	print("YULMUTATOR: " + _info);
	print("Before");
	print(SaveMessageAsText(*m_protobufMsg));

}

void MutationInfo::exitInfo()
{
	print("After");
	print(SaveMessageAsText(*m_protobufMsg));
}

template <typename T>
void YulProtoMutator::functionWrapper(
	FuzzMutatorCallback<T> const& _callback,
	T* _message,
	unsigned int _seed,
	unsigned _period,
	string const& _info,
	PrintChanges _printChanges)
{
	if (_seed % _period == 0)
	{
		if (_printChanges == PrintChanges::Yes)
		{
			MutationInfo m{_message, _info};
			_callback(_message, _seed);
		}
		else
			_callback(_message, _seed);
	}
}

// Add assignment to m/s/calldataload(0)
static YPR<AssignmentStatement> assignLoadZero(
	[](AssignmentStatement* _message, unsigned int _seed)
	{
		YPM::functionWrapper<AssignmentStatement>(
			[](AssignmentStatement* _message, unsigned int _seed)
			{
				_message->clear_expr();
				_message->set_allocated_expr(YPM::loadFromZero(_seed / 17));
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Assign load from zero"
		);
	}
);

// Invert condition of an if statement
static YPR<IfStmt> invertIfCondition(
	[](IfStmt* _message, unsigned int _seed)
	{
		YPM::functionWrapper<IfStmt>(
			[](IfStmt* _message, unsigned int)
			{
				if (_message->has_cond())
				{
					auto notOp = new UnaryOp();
					notOp->set_op(UnaryOp::NOT);
					auto oldCond = _message->release_cond();
					notOp->set_allocated_operand(oldCond);
					auto ifCond = new Expression();
					ifCond->set_allocated_unop(notOp);
					_message->set_allocated_cond(ifCond);
				}
			},
			_message,
			_seed,
			YPM::s_mediumIP,
			"If condition inverted"
		);
	}
);

// Remove inverted condition in if statement
static YPR<IfStmt> revertIfCondition(
	[](IfStmt* _message, unsigned int _seed)
	{
		YPM::functionWrapper<IfStmt>(
			[](IfStmt* _message, unsigned int)
			{
				if (_message->has_cond() && _message->cond().has_unop() &&
						_message->cond().unop().has_op() && _message->cond().unop().op() == UnaryOp::NOT)
				{
					auto oldCondition = _message->release_cond();
					auto unop = oldCondition->release_unop();
					auto conditionWithoutNot = unop->release_operand();
					_message->set_allocated_cond(conditionWithoutNot);
					delete (oldCondition);
					delete (unop);
				}
			},
			_message,
			_seed,
			YPM::s_mediumIP,
			"If condition reverted"
		);
	}
);

// Append break statement to a statement block
static YPR<Block> addBreakStmt(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				_message->add_statements()->set_allocated_breakstmt(new BreakStmt());
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Break statement added"
		);
	}
);

// Remove break statement in body of a for-loop statement
static YPR<ForStmt> removeBreakStmt(
	[](ForStmt* _message, unsigned int _seed)
	{
		YPM::functionWrapper<ForStmt>(
			[](ForStmt* _message, unsigned int)
			{
				if (_message->has_for_body())
					for (auto& stmt: *_message->mutable_for_body()->mutable_statements())
						if (stmt.has_breakstmt())
						{
							delete stmt.release_breakstmt();
							stmt.clear_breakstmt();
							break;
						}
			},
			_message,
			_seed,
			YPM::s_mediumIP,
			"Break statement removed"
		);
	}
);

// Add continue statement to statement block.
static YPR<Block> addContStmt(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				_message->add_statements()->set_allocated_contstmt(new ContinueStmt());
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Continue statement added"
		);
	}
);

/// Remove continue statement from for-loop body
static YPR<ForStmt> removeContinueStmt(
	[](ForStmt* _message, unsigned int _seed)
	{
		YPM::functionWrapper<ForStmt>(
			[](ForStmt* _message, unsigned int)
			{
				if (_message->has_for_body())
					for (auto& stmt: *_message->mutable_for_body()->mutable_statements())
						if (stmt.has_contstmt())
						{
							delete stmt.release_contstmt();
							stmt.clear_contstmt();
							break;
						}
			},
			_message,
			_seed,
			YPM::s_mediumIP,
			"Continue statement removed"
		);
	}
);

/// Mutate expression into an s/m/calldataload
static YPR<Expression> addLoadZero(
	[](Expression* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Expression>(
			[](Expression* _message, unsigned int _seed)
			{
				YPM::clearExpr(_message);
				auto tmp = YPM::loadExpression(_seed/23);
				_message->CopyFrom(*tmp);
				delete tmp;
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Expression mutated to a load operation"
		);
	}
);

/// Remove unary operation containing a load from memory/storage/calldata
static YPR<UnaryOp> removeLoad(
	[](UnaryOp* _message, unsigned int _seed)
	{
		YPM::functionWrapper<UnaryOp>(
			[](UnaryOp* _message, unsigned int)
			{
				auto operation = _message->op();
				if (operation == UnaryOp::MLOAD || operation == UnaryOp::SLOAD ||
					operation == UnaryOp::CALLDATALOAD)
				{
					delete _message->release_operand();
					_message->clear_op();
				}
			},
			_message,
			_seed,
			YPM::s_mediumIP,
			"Remove load operation"
		);
	}
);

/// Add m/sstore(0, variable)
static YPR<Block> addStoreToZero(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
		[](Block* _message, unsigned int _seed)
		{
			auto storeStmt = new StoreFunc();
			storeStmt->set_st(YPM::EnumTypeConverter<StoreFunc_Storage>{}.enumFromSeed(_seed / 37));
			storeStmt->set_allocated_loc(YPM::litExpression(0));
			storeStmt->set_allocated_val(YPM::refExpression(_seed / 11));
			auto stmt = _message->add_statements();
			stmt->set_allocated_storage_func(storeStmt);
		},
		_message,
		_seed,
		YPM::s_highIP,
		"Store to zero added"
		);
	}
);

static YPR<Block> removeStore(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				for (auto &stmt: *_message->mutable_statements())
					if (stmt.has_storage_func())
					{
						delete stmt.release_storage_func();
						stmt.clear_storage_func();
						break;
					}
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Remove store"
		);
	}
);

static YPR<ForStmt> invertForCondition(
	[](ForStmt* _message, unsigned int _seed)
	{
		YPM::functionWrapper<ForStmt>(
			[](ForStmt* _message, unsigned int)
			{
				if (_message->has_for_cond())
				{
					auto notOp = new UnaryOp();
					notOp->set_op(UnaryOp::NOT);
					auto oldCond = _message->release_for_cond();
					notOp->set_allocated_operand(oldCond);
					auto forCond = new Expression();
					forCond->set_allocated_unop(notOp);
					_message->set_allocated_for_cond(forCond);
				}
				else
					_message->set_allocated_for_cond(new Expression());
			},
			_message,
			_seed,
			YPM::s_mediumIP,
			"For condition inverted"
		);
	}
);

/// Uninvert condition of a for statement
static YPR<ForStmt> uninvertForCondition(
	[](ForStmt* _message, unsigned int _seed)
	{
		YPM::functionWrapper<ForStmt>(
			[](ForStmt* _message, unsigned int)
			{
				if (_message->has_for_cond() && _message->for_cond().has_unop() &&
					_message->for_cond().unop().has_op() && _message->for_cond().unop().op() == UnaryOp::NOT)
				{
					auto oldCondition = _message->release_for_cond();
					auto unop = oldCondition->release_unop();
					auto newCondition = unop->release_operand();
					_message->set_allocated_for_cond(newCondition);
				}
			},
			_message,
			_seed,
			YPM::s_mediumIP,
			"Uninvert for condition"
		);
	}
);

/// Make for loop condition a function call that returns a single value
static YPR<ForStmt> funcCallForCondition(
	[](ForStmt* _message, unsigned int _seed)
	{
		YPM::functionWrapper<ForStmt>(
			[](ForStmt* _message, unsigned int _seed)
			{
				if (_message->has_for_cond())
				{
					_message->clear_for_cond();
					auto functionCall = new FunctionCall();
					functionCall->set_ret(FunctionCall::SINGLE);
					functionCall->set_func_index(_seed/41);
					auto forCondExpr = new Expression();
					forCondExpr->set_allocated_func_expr(functionCall);
					_message->set_allocated_for_cond(forCondExpr);
				}
			},
			_message,
			_seed,
			YPM::s_mediumIP,
			"Function call in for condition added"
		);
	}
)
;

/// Define an identity function y = x
static YPR<Block> identityFunction(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int _seed)
			{
				auto functionDef = new FunctionDef();
				functionDef->set_num_input_params(1);
				functionDef->set_num_output_params(1);
				auto functionBlock = new Block();
				auto assignmentStatement = new AssignmentStatement();
				auto varRef = YPM::varRef(_seed / 47);
				assignmentStatement->set_allocated_ref_id(varRef);
				auto rhs = new Expression();
				auto rhsRef = YPM::varRef(_seed / 79);
				rhs->set_allocated_varref(rhsRef);
				assignmentStatement->set_allocated_expr(rhs);
				auto stmt = functionBlock->add_statements();
				stmt->set_allocated_assignment(assignmentStatement);
				functionDef->set_allocated_block(functionBlock);
				auto funcdefStmt = _message->add_statements();
				funcdefStmt->set_allocated_funcdef(functionDef);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Identity function added"
		);
	}
);

// Add leave statement to a statement block
static YPR<Block> addLeave(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				_message->add_statements()->set_allocated_leave(new LeaveStmt());
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Add leave to statement block"
		);
	}
);

// Remove leave statement from function statement-block.
static YPR<FunctionDef> removeLeave(
	[](FunctionDef* _message, unsigned int _seed)
	{
		YPM::functionWrapper<FunctionDef>(
			[](FunctionDef* _message, unsigned int)
			{
				for (auto &stmt: *_message->mutable_block()->mutable_statements())
					if (stmt.has_leave())
					{
						delete stmt.release_leave();
						stmt.clear_leave();
						break;
					}
			},
			_message,
			_seed,
			YPM::s_lowIP,
			"Remove leave from function statement block"
		);
	}
);

// Add assignment to block
static YPR<Block> addAssignment(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int _seed)
			{
				auto assignmentStatement = new AssignmentStatement();
				auto varRef = YPM::varRef(_seed / 11);
				assignmentStatement->set_allocated_ref_id(varRef);
				auto rhs = YPM::varRef(_seed / 17);
				auto rhsExpr = new Expression();
				rhsExpr->set_allocated_varref(rhs);
				assignmentStatement->set_allocated_expr(rhsExpr);
				auto newStmt = _message->add_statements();
				newStmt->set_allocated_assignment(assignmentStatement);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Add assignment to statement block"
		);
	}
);

// Remove assignment from block
static YPR<Block> removeAssignment(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				for (auto &stmt: *_message->mutable_statements())
					if (stmt.has_assignment())
					{
						delete stmt.release_assignment();
						stmt.clear_assignment();
						break;
					}
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Remove assignment from statement block"
		);
	}
);

// Add constant assignment
static YPR<Block> addConstantAssignment(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int _seed)
			{
				auto assignmentStatement = new AssignmentStatement();
				assignmentStatement->set_allocated_ref_id(
					YPM::varRef(_seed / 11)
				);
				assignmentStatement->set_allocated_expr(
					YPM::litExpression(_seed / 59)
				);
				_message->add_statements()->set_allocated_assignment(assignmentStatement);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Add constant assignment to statement block"
		);
	}
);

// Add if statement
static YPR<Block> addIfStmt(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int _seed)
			{
				auto ifStmt = new IfStmt();
				ifStmt->set_allocated_cond(YPM::refExpression(_seed / 11));
				// Add an assignment inside if
				auto ifBody = new Block();
				auto ifAssignment = new AssignmentStatement();
				ifAssignment->set_allocated_ref_id(YPM::varRef(_seed / 13));
				ifAssignment->set_allocated_expr(YPM::refExpression(_seed / 17));
				auto ifBodyStmt = ifBody->add_statements();
				ifBodyStmt->set_allocated_assignment(ifAssignment);
				ifStmt->set_allocated_if_body(ifBody);
				_message->add_statements()->set_allocated_ifstmt(ifStmt);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Add if statement to statement block"
		);
	}
);

// Remove if statement
static YPR<Block> removeIfStmt(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				for (auto &stmt: *_message->mutable_statements())
					if (stmt.has_ifstmt())
					{
						delete stmt.release_ifstmt();
						stmt.clear_ifstmt();
						break;
					}
			},
			_message,
			_seed,
			YPM::s_lowIP,
			"Remove if statement from statement block"
		);
	}
);

// Add switch statement
static YPR<Block> addSwitchStmt(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int _seed)
			{
				auto switchStmt = new SwitchStmt();
				switchStmt->add_case_stmt();
				Expression *switchExpr = new Expression();
				switchExpr->set_allocated_varref(YPM::varRef(_seed / 11));
				switchStmt->set_allocated_switch_expr(switchExpr);
				_message->add_statements()->set_allocated_switchstmt(switchStmt);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Add switch statement to statement block"
		);
	}
);

// Remove switch statement
static YPR<Block> removeSwitchStmt(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				for (auto &stmt: *_message->mutable_statements())
					if (stmt.has_switchstmt())
					{
						delete stmt.release_switchstmt();
						stmt.clear_switchstmt();
						break;
					}
			},
			_message,
			_seed,
			YPM::s_lowIP,
			"Remove switch statement from statement block"
		);
	}
);

// Add function call
static YPR<Block> addFuncCall(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int _seed)
			{
				auto call = new FunctionCall();
				YPM::configureCall(call, _seed);
				_message->add_statements()->set_allocated_functioncall(call);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Add function call to statement block"
		);
	}
);

// Remove function call
static YPR<Block> removeFuncCall(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				for (auto &stmt: *_message->mutable_statements())
					if (stmt.has_functioncall())
					{
						delete stmt.release_functioncall();
						stmt.clear_functioncall();
						break;
					}
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Remove function call from statement block"
		);
	}
);

// Add variable declaration
static YPR<Block> addVarDecl(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				_message->add_statements()->set_allocated_decl(new VarDecl());
				// Hoist var decl to beginning of block
				if (_message->statements_size() > 1)
					_message->mutable_statements(0)->Swap(
						_message->mutable_statements(_message->statements_size() - 1)
					);
			},
			_message,
			_seed,
			1,
			"Add variable declaration to statement block"
		);
	}
);

// Add multivar decl
static YPR<Block> addMultiVarDecl(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int _seed)
			{
				auto decl = new MultiVarDecl();
				decl->set_num_vars(_seed/17);
				_message->add_statements()->set_allocated_multidecl(decl);
				// Hoist multi var decl to beginning of block
				if (_message->statements_size() > 1)
					_message->mutable_statements(0)->Swap(
						_message->mutable_statements(_message->statements_size() - 1)
					);
			},
			_message,
			_seed,
			1,
			"Add multi variable declaration to statement block"
		);
	}
);

// Remove variable declaration
static YPR<Block> removeVarDecl(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				for (auto &stmt: *_message->mutable_statements())
					if (stmt.has_decl())
					{
						delete stmt.release_decl();
						stmt.clear_decl();
						break;
					}
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Remove variable declaration from statement block"
		);
	}
);

// Remove multi variable declaration
static YPR<Block> removeMultiVarDecl(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				for (auto &stmt: *_message->mutable_statements())
					if (stmt.has_multidecl())
					{
						delete stmt.release_multidecl();
						stmt.clear_multidecl();
						break;
					}
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Remove multi variable declaration from statement block"
		);
	}
);

// Add function definition
static YPR<Block> addFuncDef(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int _seed)
			{
				auto funcDef = new FunctionDef();
				funcDef->set_num_input_params(_seed/11);
				funcDef->set_num_output_params(_seed/17);
				funcDef->set_allocated_block(new Block());
				// TODO: Add assignments to output params if any
				_message->add_statements()->set_allocated_funcdef(funcDef);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Add function definition to statement block"
		);
	}
);

// Remove function definition
static YPR<Block> removeFuncDef(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				for (auto &stmt: *_message->mutable_statements())
					if (stmt.has_funcdef())
					{
						delete stmt.release_funcdef();
						stmt.clear_funcdef();
						break;
					}
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Remove function definition from statement block"
		);
	}
);

// Add bounded for stmt
static YPR<Block> addBoundedFor(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				_message->add_statements()->set_allocated_boundedforstmt(new BoundedForStmt());
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Add bounded for statement to statement block"
		);
	}
);

// Remove bounded for stmt
static YPR<Block> removeBoundedFor(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				for (auto &stmt: *_message->mutable_statements())
					if (stmt.has_boundedforstmt())
					{
						delete stmt.release_boundedforstmt();
						stmt.clear_boundedforstmt();
						break;
					}
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Remove bounded for statement from statement block"
		);
	}
);

// Add generic for stmt
static YPR<Block> addGenericFor(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				_message->add_statements()->set_allocated_forstmt(new ForStmt());
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Add for statement to statement block"
		);
	}
);

// Remove generic for stmt
static YPR<Block> removeGenericFor(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				for (auto &stmt: *_message->mutable_statements())
					if (stmt.has_forstmt())
					{
						delete stmt.release_forstmt();
						stmt.clear_forstmt();
						break;
					}
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Remove for statement from statement block"
		);
	}
);

// Add revert stmt
static YPR<Block> addRevert(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				auto termStmt = new TerminatingStmt();
				auto revertStmt = new RetRevStmt();
				revertStmt->set_stmt(RetRevStmt::REVERT);
				revertStmt->set_allocated_pos(YPM::litExpression(0));
				revertStmt->set_allocated_size(YPM::litExpression(0));
				termStmt->set_allocated_ret_rev(revertStmt);
				_message->add_statements()->set_allocated_terminatestmt(termStmt);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Add revert(0,0) statement to statement block"
		);
	}
);

// Remove revert statement
static YPR<Block> removeRevert(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				for (auto &stmt: *_message->mutable_statements())
					if (stmt.has_terminatestmt() && stmt.terminatestmt().has_ret_rev() &&
						stmt.terminatestmt().ret_rev().stmt() == RetRevStmt::REVERT)
					{
						delete stmt.release_terminatestmt();
						stmt.clear_terminatestmt();
						break;
					}
			},
			_message,
			_seed,
			YPM::s_lowIP,
			"Remove revert statement from statement block"
		);
	}
);

// Mutate nullary op
static YPR<NullaryOp> mutateNullaryOp(
	[](NullaryOp* _message, unsigned int _seed)
	{
		YPM::functionWrapper<NullaryOp>(
			[](NullaryOp* _message, unsigned int _seed)
			{
				_message->clear_op();
				_message->set_op(
					YPM::EnumTypeConverter<NullaryOp_NOp>{}.enumFromSeed(_seed / 11)
				);
			},
			_message,
			_seed,
			YPM::s_mediumIP,
			"Mutate nullary operation in expression"
		);
	}
);

// Mutate binary op
static YPR<BinaryOp> mutateBinaryOp(
	[](BinaryOp* _message, unsigned int _seed)
	{
		YPM::functionWrapper<BinaryOp>(
			[](BinaryOp* _message, unsigned int _seed)
			{
				_message->clear_op();
				_message->set_op(
					YPM::EnumTypeConverter<BinaryOp_BOp>{}.enumFromSeed(_seed / 11)
				);
			},
			_message,
			_seed,
			YPM::s_mediumIP,
			"Mutate binary operation in expression"
		);
	}
);

// Mutate unary op
static YPR<UnaryOp> mutateUnaryOp(
	[](UnaryOp* _message, unsigned int _seed)
	{
		YPM::functionWrapper<UnaryOp>(
			[](UnaryOp* _message, unsigned int _seed)
			{
				_message->clear_op();
				_message->set_op(
					YPM::EnumTypeConverter<UnaryOp_UOp>{}.enumFromSeed(_seed / 11)
				);
			},
			_message,
			_seed,
			YPM::s_mediumIP,
			"Mutate unary operation in expression"
		);
	}
);

// Add pop(call())
static YPR<Block> addPopCall(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int _seed)
			{
				auto call = new LowLevelCall();
				call->set_callty(
					YPM::EnumTypeConverter<LowLevelCall_Type>{}.enumFromSeed(_seed/13)
				);
				auto popExpr = new Expression();
				popExpr->set_allocated_lowcall(call);
				auto popStmt = new PopStmt();
				popStmt->set_allocated_expr(popExpr);
				_message->add_statements()->set_allocated_pop(popStmt);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Add pop(call) statement to statement block"
		);
	}
);

// Remove pop
static YPR<Block> removePop(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int)
			{
				for (auto &stmt: *_message->mutable_statements())
					if (stmt.has_pop())
					{
						delete stmt.release_pop();
						stmt.clear_pop();
						break;
					}
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Remove pop statement from statement block"
		);
	}
);

// Add pop(create)
static YPR<Block> addPopCreate(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int _seed)
			{
				auto create = new Create();
				create->set_createty(
					YPM::EnumTypeConverter<Create_Type>{}.enumFromSeed(_seed/17)
				);
				auto popExpr = new Expression();
				popExpr->set_allocated_create(create);
				auto popStmt = new PopStmt();
				popStmt->set_allocated_expr(popExpr);
				_message->add_statements()->set_allocated_pop(popStmt);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Add pop(create) statement to statement block"
		);
	}
);

// Add pop(f()) where f() -> r is a user-defined function.
// Assumes that f() already exists, if it doesn't this turns into pop(constant).
static YPR<Block> addPopUserFunc(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int _seed)
			{
				auto functioncall = new FunctionCall();
				functioncall->set_ret(FunctionCall::SINGLE);
				YPM::configureCallArgs(FunctionCall::SINGLE, functioncall, _seed);
				auto funcExpr = new Expression();
				funcExpr->set_allocated_func_expr(functioncall);
				auto popStmt = new PopStmt();
				popStmt->set_allocated_expr(funcExpr);
				_message->add_statements()->set_allocated_pop(popStmt);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Add pop(f()) statement to statement block"
		);
	}
);

// Add function call in another function's body
static YPR<Block> addFuncCallInFuncBody(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int _seed)
			{
				auto functioncall = new FunctionCall();
				YPM::configureCall(functioncall, _seed);
				_message->add_statements()->set_allocated_functioncall(functioncall);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Add function call in function body"
		);
	}
);

// Remove function call from a function's body
static YPR<FunctionDef> removeFuncCallInFuncBody(
	[](FunctionDef* _message, unsigned int _seed)
	{
		YPM::functionWrapper<FunctionDef>(
			[](FunctionDef* _message, unsigned int)
			{
				for (auto &stmt: *_message->mutable_block()->mutable_statements())
					if (stmt.has_functioncall())
					{
						delete stmt.release_functioncall();
						stmt.clear_functioncall();
						break;
					}
			},
			_message,
			_seed,
			YPM::s_mediumIP,
			"Remove function call from function body"
		);
	}
);

// Add dataoffset/datasize
static YPR<Expression> addDataExpr(
	[](Expression* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Expression>(
			[](Expression* _message, unsigned int _seed)
			{
				YPM::clearExpr(_message);
				auto unopdata = new UnaryOpData();
				auto objId = new ObjectId();
				objId->set_id(_seed/23);
				unopdata->set_allocated_identifier(objId);
				unopdata->set_op(
					YPM::EnumTypeConverter<UnaryOpData_UOpData>{}.enumFromSeed(_seed/29)
				);
				_message->set_allocated_unopdata(unopdata);
			},
			_message,
			_seed,
			YPM::s_mediumIP,
			"Mutate expression to dataoffset/size"
		);
	}
);

// Add variable reference inside for-loop body
static YPR<BoundedForStmt> addVarRefInForBody(
	[](BoundedForStmt* _message, unsigned int _seed)
	{
		YPM::functionWrapper<BoundedForStmt>(
			[](BoundedForStmt* _message, unsigned int _seed)
			{
				auto popStmt = new PopStmt();
				popStmt->set_allocated_expr(YPM::refExpression(_seed / 31));
				_message->mutable_for_body()->add_statements()->set_allocated_pop(popStmt);
			},
			_message,
			_seed,
			YPM::s_mediumIP,
			"Add variable reference in for loop body"
		);
	}
);

// Mutate expression to a function call
static YPR<Expression> mutateExprToFuncCall(
	[](Expression* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Expression>(
			[](Expression* _message, unsigned int _seed)
			{
				YPM::clearExpr(_message);
				auto functionCall = new FunctionCall();
				functionCall->set_ret(FunctionCall::SINGLE);
				functionCall->set_func_index(_seed/17);
				_message->set_allocated_func_expr(functionCall);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Mutate expression to function call"
		);
	}
);

// Mutate expression to variable reference
static YPR<Expression> mutateExprToVarRef(
	[](Expression* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Expression>(
			[](Expression* _message, unsigned int _seed)
			{
				YPM::clearExpr(_message);
				_message->set_allocated_varref(YPM::varRef(_seed/19));
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Mutate expression to a variable reference"
		);
	}
);

// Add varref to statement
static YPR<Statement> addVarRefToStmt(
	[](Statement* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Statement>(
			[](Statement* _message, unsigned int _seed)
			{
				YPM::addArgs(_message, _seed, YPM::refExpression);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Make statement arguments variable references"
		);
	}
);

// Add varrefs to statement arguments recursively
static YPR<Statement> addVarRefToStmtRec(
	[](Statement* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Statement>(
			[](Statement* _message, unsigned int _seed)
			{
				YPM::addArgsRec(_message, _seed, YPM::initOrVarRef);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Make statement arguments variable references recursively"
		);
	}
);

// Add binop expression to statement.
static YPR<Statement> addBinopToStmt(
	[](Statement* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Statement>(
			[](Statement* _message, unsigned int _seed)
			{
				YPM::addArgs(_message, _seed/7, YPM::binopExpression);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Make statement arguments binary operations"
		);
	}
);

// Mutate varref
static YPR<VarRef> mutateVarRef(
	[](VarRef* _message, unsigned int _seed)
	{
		YPM::functionWrapper<VarRef>(
			[](VarRef* _message, unsigned int _seed)
			{
				_message->set_varnum(_seed);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Mutate variable reference"
		);
	}
);

// Add load expression to statement
static YPR<Statement> addLoadToStmt(
	[](Statement* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Statement>(
			[](Statement* _message, unsigned int _seed)
			{
				YPM::addArgs(_message, _seed, YPM::loadExpression);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Mutate statement arguments to a load expression"
		);
	}
);

// Add a randomly chosen statement to a statement block
static YPR<Block> addStmt(
	[](Block* _message, unsigned int _seed)
	{
		YPM::functionWrapper<Block>(
			[](Block* _message, unsigned int _seed)
			{
				YPM::addStmt(_message, _seed);
			},
			_message,
			_seed,
			YPM::s_highIP,
			"Add pseudo randomly chosen statement type to statement block"
		);
	}
);

void YPM::addArgs(
	Statement *_stmt,
	unsigned int _seed,
	std::function<Expression *(unsigned int)> _func
)
{
	switch (_stmt->stmt_oneof_case())
	{
	case Statement::kDecl:
		if (!_stmt->decl().has_expr() || _stmt->decl().expr().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_decl()->set_allocated_expr(_func(_seed/13));
		break;
	case Statement::kAssignment:
		if (!_stmt->assignment().has_expr() || _stmt->assignment().expr().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_assignment()->set_allocated_expr(_func(_seed/17));
		if (!_stmt->assignment().has_ref_id() || _stmt->assignment().ref_id().varnum() == 0)
			_stmt->mutable_assignment()->set_allocated_ref_id(varRef(_seed/19));
		break;
	case Statement::kIfstmt:
		if (!_stmt->ifstmt().has_cond() || _stmt->ifstmt().cond().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_ifstmt()->set_allocated_cond(_func(_seed/23));
		break;
	case Statement::kStorageFunc:
		if (!_stmt->storage_func().has_loc() || _stmt->storage_func().loc().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_storage_func()->set_allocated_loc(_func(_seed/29));
		if (!_stmt->storage_func().has_val() || _stmt->storage_func().val().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_storage_func()->set_allocated_val(_func(_seed/37));
		break;
	case Statement::kBlockstmt:
		break;
	case Statement::kForstmt:
		if (!_stmt->forstmt().has_for_cond() || _stmt->forstmt().for_cond().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_forstmt()->set_allocated_for_cond(_func(_seed/41));
		break;
	case Statement::kBoundedforstmt:
		break;
	case Statement::kSwitchstmt:
		if (!_stmt->switchstmt().has_switch_expr() || _stmt->switchstmt().switch_expr().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_switchstmt()->set_allocated_switch_expr(_func(_seed/43));
		break;
	case Statement::kBreakstmt:
		break;
	case Statement::kContstmt:
		break;
	case Statement::kLogFunc:
		if (!_stmt->log_func().has_pos() || _stmt->log_func().pos().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_log_func()->set_allocated_pos(_func(_seed/19));
		if (!_stmt->log_func().has_size() || _stmt->log_func().size().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_log_func()->set_allocated_size(_func(_seed/17));
		if (!_stmt->log_func().has_t1() || _stmt->log_func().t1().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_log_func()->set_allocated_t1(_func(_seed/7));
		if (!_stmt->log_func().has_t2() || _stmt->log_func().t2().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_log_func()->set_allocated_t2(_func(_seed/5));
		if (!_stmt->log_func().has_t3() || _stmt->log_func().t3().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_log_func()->set_allocated_t3(_func(_seed/3));
		if (!_stmt->log_func().has_t4() || _stmt->log_func().t4().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_log_func()->set_allocated_t4(_func(_seed));
		break;
	case Statement::kCopyFunc:
		if (!_stmt->copy_func().has_target() || _stmt->copy_func().target().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_copy_func()->set_allocated_target(_func(_seed/17));
		if (!_stmt->copy_func().has_source() || _stmt->copy_func().source().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_copy_func()->set_allocated_source(_func(_seed/13));
		if (!_stmt->copy_func().has_size() || _stmt->copy_func().size().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_copy_func()->set_allocated_size(_func(_seed/11));
		break;
	case Statement::kExtcodeCopy:
		if (!_stmt->extcode_copy().has_addr() || _stmt->extcode_copy().addr().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_extcode_copy()->set_allocated_addr(_func(_seed/19));
		if (!_stmt->extcode_copy().has_target() || _stmt->extcode_copy().target().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_extcode_copy()->set_allocated_target(_func(_seed/17));
		if (!_stmt->extcode_copy().has_source() || _stmt->extcode_copy().source().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_extcode_copy()->set_allocated_source(_func(_seed/13));
		if (!_stmt->extcode_copy().has_size() || _stmt->extcode_copy().size().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_extcode_copy()->set_allocated_size(_func(_seed/11));
		break;
	case Statement::kTerminatestmt:
		break;
	case Statement::kFunctioncall:
		if (!_stmt->functioncall().has_in_param1() || _stmt->functioncall().in_param1().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_functioncall()->set_allocated_in_param1(_func(_seed/101));
		if (!_stmt->functioncall().has_in_param2() || _stmt->functioncall().in_param2().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_functioncall()->set_allocated_in_param2(_func(_seed/103));
		if (!_stmt->functioncall().has_in_param3() || _stmt->functioncall().in_param3().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_functioncall()->set_allocated_in_param3(_func(_seed/107));
		if (!_stmt->functioncall().has_in_param4() || _stmt->functioncall().in_param4().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_functioncall()->set_allocated_in_param4(_func(_seed/113));
		if (!_stmt->functioncall().has_out_param1() || _stmt->functioncall().out_param1().varnum() == 0)
			_stmt->mutable_functioncall()->set_allocated_out_param1(varRef(_seed/117));
		if (!_stmt->functioncall().has_out_param2() || _stmt->functioncall().out_param2().varnum() == 0)
			_stmt->mutable_functioncall()->set_allocated_out_param2(varRef(_seed/119));
		if (!_stmt->functioncall().has_out_param3() || _stmt->functioncall().out_param3().varnum() == 0)
			_stmt->mutable_functioncall()->set_allocated_out_param3(varRef(_seed/123));
		if (!_stmt->functioncall().has_out_param4() || _stmt->functioncall().out_param4().varnum() == 0)
			_stmt->mutable_functioncall()->set_allocated_out_param4(varRef(_seed/129));
		break;
	case Statement::kFuncdef:
		break;
	case Statement::kPop:
		if (!_stmt->pop().has_expr() || _stmt->pop().expr().expr_oneof_case() == Expression::EXPR_ONEOF_NOT_SET)
			_stmt->mutable_pop()->set_allocated_expr(_func(_seed/51));
		break;
	case Statement::kLeave:
		break;
	case Statement::kMultidecl:
		break;
	case Statement::STMT_ONEOF_NOT_SET:
		break;
	}
}

void YPM::addArgsRec(
	Statement *_stmt,
	unsigned int _seed,
	std::function<void(Expression*, unsigned int)> _func
)
{
	switch (_stmt->stmt_oneof_case())
	{
	case Statement::kDecl:
		_func(_stmt->mutable_decl()->mutable_expr(), _seed/13);
		break;
	case Statement::kAssignment:
		_stmt->mutable_assignment()->mutable_ref_id()->set_varnum(_seed/19);
		_func(_stmt->mutable_assignment()->mutable_expr(), _seed/17);
		break;
	case Statement::kIfstmt:
		_func(_stmt->mutable_ifstmt()->mutable_cond(), _seed/23);
		break;
	case Statement::kStorageFunc:
		_func(_stmt->mutable_storage_func()->mutable_loc(), _seed/29);
		_func(_stmt->mutable_storage_func()->mutable_val(), _seed/37);
		break;
	case Statement::kBlockstmt:
		break;
	case Statement::kForstmt:
		_func(_stmt->mutable_forstmt()->mutable_for_cond(), _seed/41);
		break;
	case Statement::kBoundedforstmt:
		break;
	case Statement::kSwitchstmt:
		_func(_stmt->mutable_switchstmt()->mutable_switch_expr(), _seed/43);
		break;
	case Statement::kBreakstmt:
		break;
	case Statement::kContstmt:
		break;
	case Statement::kLogFunc:
		_func(_stmt->mutable_log_func()->mutable_pos(), _seed/19);
		_func(_stmt->mutable_log_func()->mutable_size(), _seed/17);
		_func(_stmt->mutable_log_func()->mutable_t1(), _seed/7);
		_func(_stmt->mutable_log_func()->mutable_t2(), _seed/5);
		_func(_stmt->mutable_log_func()->mutable_t3(), _seed/3);
		_func(_stmt->mutable_log_func()->mutable_t4(), _seed);
		break;
	case Statement::kCopyFunc:
		_func(_stmt->mutable_copy_func()->mutable_target(), _seed/17);
		_func(_stmt->mutable_copy_func()->mutable_source(), _seed/13);
		_func(_stmt->mutable_copy_func()->mutable_size(), _seed/11);
		break;
	case Statement::kExtcodeCopy:
		_func(_stmt->mutable_extcode_copy()->mutable_addr(), _seed/19);
		_func(_stmt->mutable_extcode_copy()->mutable_target(), _seed/17);
		_func(_stmt->mutable_extcode_copy()->mutable_source(), _seed/13);
		_func(_stmt->mutable_extcode_copy()->mutable_size(), _seed/11);
		break;
	case Statement::kTerminatestmt:
		if (_stmt->terminatestmt().term_oneof_case() == TerminatingStmt::kRetRev)
		{
			_func(_stmt->mutable_terminatestmt()->mutable_ret_rev()->mutable_pos(), _seed/11);
			_func(_stmt->mutable_terminatestmt()->mutable_ret_rev()->mutable_size(), _seed/13);
		}
		else if (_stmt->terminatestmt().term_oneof_case() == TerminatingStmt::kSelfDes)
			_func(_stmt->mutable_terminatestmt()->mutable_self_des()->mutable_addr(), _seed/17);
		break;
	case Statement::kFunctioncall:
		_func(_stmt->mutable_functioncall()->mutable_in_param1(), _seed/11);
		_func(_stmt->mutable_functioncall()->mutable_in_param2(), _seed/13);
		_func(_stmt->mutable_functioncall()->mutable_in_param3(), _seed/17);
		_func(_stmt->mutable_functioncall()->mutable_in_param4(), _seed/19);
		if (_stmt->functioncall().ret() == FunctionCall_Returns::FunctionCall_Returns_MULTIASSIGN)
		{
			_stmt->mutable_functioncall()->set_allocated_out_param1(varRef(_seed/23));
			_stmt->mutable_functioncall()->set_allocated_out_param2(varRef(_seed/29));
			_stmt->mutable_functioncall()->set_allocated_out_param3(varRef(_seed/31));
			_stmt->mutable_functioncall()->set_allocated_out_param4(varRef(_seed/37));
		}
		break;
	case Statement::kFuncdef:
		break;
	case Statement::kPop:
		_func(_stmt->mutable_pop()->mutable_expr(), _seed/11);
		break;
	case Statement::kLeave:
		break;
	case Statement::kMultidecl:
		break;
	case Statement::STMT_ONEOF_NOT_SET:
		break;
	}
}

void YPM::addStmt(Block* _block, unsigned _seed)
{
	auto stmt = _block->add_statements();
	switch ((_seed / 17) % 19)
	{
	case 0:
		stmt->set_allocated_decl(new VarDecl());
		break;
	case 1:
		stmt->set_allocated_assignment(new AssignmentStatement());
		break;
	case 2:
		stmt->set_allocated_ifstmt(new IfStmt());
		break;
	case 3:
		stmt->set_allocated_storage_func(new StoreFunc());
		break;
	case 4:
		stmt->set_allocated_blockstmt(new Block());
		break;
	case 5:
		stmt->set_allocated_forstmt(new ForStmt());
		break;
	case 6:
		stmt->set_allocated_switchstmt(new SwitchStmt());
		break;
	case 7:
		stmt->set_allocated_breakstmt(new BreakStmt());
		break;
	case 8:
		stmt->set_allocated_contstmt(new ContinueStmt());
		break;
	case 9:
		stmt->set_allocated_log_func(new LogFunc());
		break;
	case 10:
		stmt->set_allocated_copy_func(new CopyFunc());
		break;
	case 11:
		stmt->set_allocated_extcode_copy(new ExtCodeCopy());
		break;
	case 12:
		stmt->set_allocated_terminatestmt(new TerminatingStmt());
		break;
	case 13:
		stmt->set_allocated_functioncall(new FunctionCall());
		break;
	case 14:
		stmt->set_allocated_boundedforstmt(new BoundedForStmt());
		break;
	case 15:
		stmt->set_allocated_funcdef(new FunctionDef());
		break;
	case 16:
		stmt->set_allocated_pop(new PopStmt());
		break;
	case 17:
		stmt->set_allocated_leave(new LeaveStmt());
		break;
	case 18:
		stmt->set_allocated_multidecl(new MultiVarDecl());
		break;
	}
}

Literal* YPM::intLiteral(unsigned _value)
{
	auto lit = new Literal();
	lit->set_intval(_value);
	return lit;
}
//
Expression* YPM::litExpression(unsigned _value)
{
	auto lit = intLiteral(_value);
	auto expr = new Expression();
	expr->set_allocated_cons(lit);
	return expr;
}

VarRef* YPM::varRef(unsigned _seed)
{
	auto varref = new VarRef();
	varref->set_varnum(_seed);
	return varref;
}

Expression* YPM::refExpression(unsigned _seed)
{
	auto refExpr = new Expression();
	refExpr->set_allocated_varref(varRef(_seed));
	return refExpr;
}

void YPM::configureCall(FunctionCall *_call, unsigned int _seed)
{
	auto type = EnumTypeConverter<FunctionCall_Returns>{}.enumFromSeed(_seed);
	_call->set_ret(type);
	_call->set_func_index(_seed);
	configureCallArgs(type, _call, _seed);
}

void YPM::configureCallArgs(
	FunctionCall_Returns _callType,
	FunctionCall *_call,
	unsigned _seed
)
{
	// Configuration rules:
	// All function calls must configure four input arguments, because
	// a function of any type may have at most four input arguments.
	// Out arguments need to be configured only for multi-assign
	switch (_callType)
	{
	case FunctionCall_Returns_MULTIASSIGN:
	{
		auto outRef4 = YPM::varRef(_seed / 1337);
		_call->set_allocated_out_param4(outRef4);

		auto outRef3 = YPM::varRef(_seed / 7);
		_call->set_allocated_out_param3(outRef3);

		auto outRef2 = YPM::varRef(_seed / 101);
		_call->set_allocated_out_param2(outRef2);

		auto outRef1 = YPM::varRef(_seed / 100001);
		_call->set_allocated_out_param1(outRef1);
	}
	BOOST_FALLTHROUGH;
	case FunctionCall_Returns_MULTIDECL:
	BOOST_FALLTHROUGH;
	case FunctionCall_Returns_SINGLE:
	BOOST_FALLTHROUGH;
	case FunctionCall_Returns_ZERO:
	{
		auto inArg4 = new Expression();
		auto inRef4 = YPM::varRef(_seed / 4);
		inArg4->set_allocated_varref(inRef4);
		_call->set_allocated_in_param4(inArg4);

		auto inArg3 = new Expression();
		auto inRef3 = YPM::varRef(_seed / 3);
		inArg3->set_allocated_varref(inRef3);
		_call->set_allocated_in_param3(inArg3);

		auto inArg2 = new Expression();
		auto inRef2 = YPM::varRef(_seed / 2);
		inArg2->set_allocated_varref(inRef2);
		_call->set_allocated_in_param2(inArg2);

		auto inArg1 = new Expression();
		auto inRef1 = YPM::varRef(_seed);
		inArg1->set_allocated_varref(inRef1);
		_call->set_allocated_in_param1(inArg1);
		break;
	}
	}
}

template <typename T>
T YPM::EnumTypeConverter<T>::validEnum(unsigned _seed)
{
	auto ret = static_cast<T>(_seed % (enumMax() - enumMin() + 1) + enumMin());
	if constexpr (std::is_same_v<std::decay_t<T>, FunctionCall_Returns>)
		yulAssert(FunctionCall_Returns_IsValid(ret), "Yul proto mutator: Invalid enum");
	else if constexpr (std::is_same_v<std::decay_t<T>, StoreFunc_Storage>)
		yulAssert(StoreFunc_Storage_IsValid(ret), "Yul proto mutator: Invalid enum");
	else if constexpr (std::is_same_v<std::decay_t<T>, NullaryOp_NOp>)
		yulAssert(NullaryOp_NOp_IsValid(ret), "Yul proto mutator: Invalid enum");
	else if constexpr (std::is_same_v<std::decay_t<T>, BinaryOp_BOp>)
		yulAssert(BinaryOp_BOp_IsValid(ret), "Yul proto mutator: Invalid enum");
	else if constexpr (std::is_same_v<std::decay_t<T>, UnaryOp_UOp>)
		yulAssert(UnaryOp_UOp_IsValid(ret), "Yul proto mutator: Invalid enum");
	else if constexpr (std::is_same_v<std::decay_t<T>, LowLevelCall_Type>)
		yulAssert(LowLevelCall_Type_IsValid(ret), "Yul proto mutator: Invalid enum");
	else if constexpr (std::is_same_v<std::decay_t<T>, Create_Type>)
		yulAssert(Create_Type_IsValid(ret), "Yul proto mutator: Invalid enum");
	else if constexpr (std::is_same_v<std::decay_t<T>, UnaryOpData_UOpData>)
		yulAssert(UnaryOpData_UOpData_IsValid(ret), "Yul proto mutator: Invalid enum");
	else
		static_assert(AlwaysFalse<T>::value, "Yul proto mutator: non-exhaustive visitor.");
	return ret;
}

template <typename T>
int YPM::EnumTypeConverter<T>::enumMax()
{
	if constexpr (std::is_same_v<std::decay_t<T>, FunctionCall_Returns>)
		return FunctionCall_Returns_Returns_MAX;
	else if constexpr (std::is_same_v<std::decay_t<T>, StoreFunc_Storage>)
		return StoreFunc_Storage_Storage_MAX;
	else if constexpr (std::is_same_v<std::decay_t<T>, NullaryOp_NOp>)
		return NullaryOp_NOp_NOp_MAX;
	else if constexpr (std::is_same_v<std::decay_t<T>, BinaryOp_BOp>)
		return BinaryOp_BOp_BOp_MAX;
	else if constexpr (std::is_same_v<std::decay_t<T>, UnaryOp_UOp>)
		return UnaryOp_UOp_UOp_MAX;
	else if constexpr (std::is_same_v<std::decay_t<T>, LowLevelCall_Type>)
		return LowLevelCall_Type_Type_MAX;
	else if constexpr (std::is_same_v<std::decay_t<T>, Create_Type>)
		return Create_Type_Type_MAX;
	else if constexpr (std::is_same_v<std::decay_t<T>, UnaryOpData_UOpData>)
		return UnaryOpData_UOpData_UOpData_MAX;
	else
		static_assert(AlwaysFalse<T>::value, "Yul proto mutator: non-exhaustive visitor.");
}

template <typename T>
int YPM::EnumTypeConverter<T>::enumMin()
{
	if constexpr (std::is_same_v<std::decay_t<T>, FunctionCall_Returns>)
		return FunctionCall_Returns_Returns_MIN;
	else if constexpr (std::is_same_v<std::decay_t<T>, StoreFunc_Storage>)
		return StoreFunc_Storage_Storage_MIN;
	else if constexpr (std::is_same_v<std::decay_t<T>, NullaryOp_NOp>)
		return NullaryOp_NOp_NOp_MIN;
	else if constexpr (std::is_same_v<std::decay_t<T>, BinaryOp_BOp>)
		return BinaryOp_BOp_BOp_MIN;
	else if constexpr (std::is_same_v<std::decay_t<T>, UnaryOp_UOp>)
		return UnaryOp_UOp_UOp_MIN;
	else if constexpr (std::is_same_v<std::decay_t<T>, LowLevelCall_Type>)
		return LowLevelCall_Type_Type_MIN;
	else if constexpr (std::is_same_v<std::decay_t<T>, Create_Type>)
		return Create_Type_Type_MIN;
	else if constexpr (std::is_same_v<std::decay_t<T>, UnaryOpData_UOpData>)
		return UnaryOpData_UOpData_UOpData_MIN;
	else
		static_assert(AlwaysFalse<T>::value, "Yul proto mutator: non-exhaustive visitor.");
}

Expression* YPM::loadExpression(unsigned _seed)
{
	auto unop = new UnaryOp();
	unop->set_allocated_operand(refExpression(_seed/17));
	switch (_seed % 3)
	{
	case 0:
		unop->set_op(UnaryOp::MLOAD);
		break;
	case 1:
		unop->set_op(UnaryOp::SLOAD);
		break;
	case 2:
		unop->set_op(UnaryOp::CALLDATALOAD);
		break;
	}
	auto expr = new Expression();
	expr->set_allocated_unop(unop);
	return expr;
}

Expression* YPM::loadFromZero(unsigned _seed)
{
	auto unop = new UnaryOp();
	unop->set_allocated_operand(litExpression(0));
	switch (_seed % 3)
	{
	case 0:
		unop->set_op(UnaryOp::MLOAD);
		break;
	case 1:
		unop->set_op(UnaryOp::SLOAD);
		break;
	case 2:
		unop->set_op(UnaryOp::CALLDATALOAD);
		break;
	}
	auto expr = new Expression();
	expr->set_allocated_unop(unop);
	return expr;
}

void YPM::clearExpr(Expression* _expr)
{
	switch (_expr->expr_oneof_case())
	{
	case Expression::kVarref:
		delete _expr->release_varref();
		_expr->clear_varref();
		break;
	case Expression::kCons:
		delete _expr->release_cons();
		_expr->clear_cons();
		break;
	case Expression::kBinop:
		delete _expr->release_binop();
		_expr->clear_binop();
		break;
	case Expression::kUnop:
		delete _expr->release_unop();
		_expr->clear_unop();
		break;
	case Expression::kTop:
		delete _expr->release_top();
		_expr->clear_top();
		break;
	case Expression::kNop:
		delete _expr->release_nop();
		_expr->clear_nop();
		break;
	case Expression::kFuncExpr:
		delete _expr->release_func_expr();
		_expr->clear_func_expr();
		break;
	case Expression::kLowcall:
		delete _expr->release_lowcall();
		_expr->clear_lowcall();
		break;
	case Expression::kCreate:
		delete _expr->release_create();
		_expr->clear_create();
		break;
	case Expression::kUnopdata:
		delete _expr->release_unopdata();
		_expr->clear_unopdata();
		break;
	case Expression::EXPR_ONEOF_NOT_SET:
		break;
	}
}

Expression* YPM::binopExpression(unsigned _seed)
{
	auto binop = new BinaryOp();
	binop->set_allocated_left(refExpression(_seed/17));
	binop->set_allocated_right(refExpression(_seed/21));
	binop->set_op(
		YPM::EnumTypeConverter<BinaryOp_BOp>{}.enumFromSeed(_seed / 23)
	);
	auto expr = new Expression();
	expr->set_allocated_binop(binop);
	return expr;
}

void YPM::initOrVarRef(Expression* _expr, unsigned _seed)
{
	switch (_expr->expr_oneof_case())
	{
	case Expression::kVarref:
		if (_expr->varref().varnum() == 0)
			_expr->mutable_varref()->set_varnum(_seed/17);
		break;
	case Expression::kCons:
		if (_expr->cons().literal_oneof_case() == Literal::LITERAL_ONEOF_NOT_SET)
			_expr->mutable_cons()->set_intval(_seed);
		break;
	case Expression::kBinop:
		if (!set(_expr->binop().left()))
			_expr->mutable_binop()->mutable_left()->set_allocated_varref(varRef(_seed));
		else
			initOrVarRef(_expr->mutable_binop()->mutable_left(), _seed/11);

		if (!set(_expr->binop().right()))
			_expr->mutable_binop()->mutable_right()->set_allocated_varref(varRef(_seed/17));
		else
			initOrVarRef(_expr->mutable_binop()->mutable_right(), _seed/17);
		break;
	case Expression::kUnop:
		if (!set(_expr->unop().operand()))
			_expr->mutable_unop()->mutable_operand()->set_allocated_varref(varRef(_seed));
		else
			initOrVarRef(_expr->mutable_unop()->mutable_operand(), _seed/23);
		break;
	case Expression::kTop:
		if (!set(_expr->top().arg1()))
			_expr->mutable_top()->mutable_arg1()->set_allocated_varref(varRef(_seed));
		else
			initOrVarRef(_expr->mutable_top()->mutable_arg1(), _seed/29);

		if (!set(_expr->top().arg2()))
			_expr->mutable_top()->mutable_arg2()->set_allocated_varref(varRef(_seed/17));
		else
			initOrVarRef(_expr->mutable_top()->mutable_arg2(), _seed/31);

		if (!set(_expr->top().arg3()))
			_expr->mutable_top()->mutable_arg3()->set_allocated_varref(varRef(_seed));
		else
			initOrVarRef(_expr->mutable_top()->mutable_arg3(), _seed/37);
		break;
	case Expression::kNop:
		break;
	case Expression::kFuncExpr:
		_expr->mutable_func_expr()->set_ret(FunctionCall_Returns::FunctionCall_Returns_SINGLE);

		if (!set(_expr->func_expr().in_param1()))
			_expr->mutable_func_expr()->mutable_in_param1()->set_allocated_varref(varRef(_seed));
		else
			initOrVarRef(_expr->mutable_func_expr()->mutable_in_param1(), _seed/41);

		if (!set(_expr->func_expr().in_param2()))
			_expr->mutable_func_expr()->mutable_in_param2()->set_allocated_varref(varRef(_seed/7));
		else
			initOrVarRef(_expr->mutable_func_expr()->mutable_in_param2(), _seed/43);

		if (!set(_expr->func_expr().in_param3()))
			_expr->mutable_func_expr()->mutable_in_param3()->set_allocated_varref(varRef(_seed/11));
		else
			initOrVarRef(_expr->mutable_func_expr()->mutable_in_param3(), _seed/47);

		if (!set(_expr->func_expr().in_param4()))
			_expr->mutable_func_expr()->mutable_in_param4()->set_allocated_varref(varRef(_seed));
		else
			initOrVarRef(_expr->mutable_func_expr()->mutable_in_param4(), _seed/53);

		break;
	case Expression::kLowcall:
		// Wei
		if (_expr->lowcall().callty() == LowLevelCall::CALLCODE || _expr->lowcall().callty() == LowLevelCall::CALL)
		{
			if (!set(_expr->lowcall().wei()))
				_expr->mutable_lowcall()->mutable_wei()->set_allocated_varref(varRef(_seed));
			else
				initOrVarRef(_expr->mutable_lowcall()->mutable_wei(), _seed/19);
		}

		// Gas
		if (!set(_expr->lowcall().gas()))
			_expr->mutable_lowcall()->mutable_gas()->set_allocated_varref(varRef(_seed/7));
		else
			initOrVarRef(_expr->mutable_lowcall()->mutable_gas(), _seed/101);

		// Addr
		if (!set(_expr->lowcall().addr()))
			_expr->mutable_lowcall()->mutable_addr()->set_allocated_varref(varRef(_seed/5));
		else
			initOrVarRef(_expr->mutable_lowcall()->mutable_addr(), _seed/103);

		// In
		if (!set(_expr->lowcall().in()))
			_expr->mutable_lowcall()->mutable_in()->set_allocated_varref(varRef(_seed/3));
		else
			initOrVarRef(_expr->mutable_lowcall()->mutable_in(), _seed/39);
		// Insize
		if (!set(_expr->lowcall().insize()))
			_expr->mutable_lowcall()->mutable_insize()->set_allocated_varref(varRef(_seed/11));
		else
			initOrVarRef(_expr->mutable_lowcall()->mutable_insize(), _seed);
		// Out
		if (!set(_expr->lowcall().out()))
			_expr->mutable_lowcall()->mutable_out()->set_allocated_varref(varRef(_seed/13));
		else
			initOrVarRef(_expr->mutable_lowcall()->mutable_out(), _seed);
		// Outsize
		if (!set(_expr->lowcall().outsize()))
			_expr->mutable_lowcall()->mutable_outsize()->set_allocated_varref(varRef(_seed/17));
		else
			initOrVarRef(_expr->mutable_lowcall()->mutable_outsize(), _seed);
		break;
	case Expression::kCreate:
		// Value
		if (_expr->create().createty() == Create_Type::Create_Type_CREATE2)
		{
			if (!set(_expr->create().value()))
				_expr->mutable_create()->mutable_value()->set_allocated_varref(varRef(_seed/5));
			else
				initOrVarRef(_expr->mutable_create()->mutable_value(), _seed);
		}
		// Wei
		if (!set(_expr->create().wei()))
			_expr->mutable_create()->mutable_wei()->set_allocated_varref(varRef(_seed/7));
		else
			initOrVarRef(_expr->mutable_create()->mutable_wei(), _seed);
		// Position
		if (!set(_expr->create().position()))
			_expr->mutable_create()->mutable_position()->set_allocated_varref(varRef(_seed/11));
		else
			initOrVarRef(_expr->mutable_create()->mutable_position(), _seed);
		// Size
		if (!set(_expr->create().size()))
			_expr->mutable_create()->mutable_size()->set_allocated_varref(varRef(_seed/13));
		else
			initOrVarRef(_expr->mutable_create()->mutable_size(), _seed);
		break;
	case Expression::kUnopdata:
		break;
	case Expression::EXPR_ONEOF_NOT_SET:
		_expr->set_allocated_varref(varRef(_seed/17));
		break;
	}
}