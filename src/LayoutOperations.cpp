/*
 * =====================================================================================
 *
 *    Description:  Corblivar layout operations
 *
 *    Copyright (C) 2013-2016 Johann Knechtel, johann aett jknechtel dot de
 *
 *    This file is part of Corblivar.
 *    
 *    Corblivar is free software: you can redistribute it and/or modify it under the terms
 *    of the GNU General Public License as published by the Free Software Foundation,
 *    either version 3 of the License, or (at your option) any later version.
 *    
 *    Corblivar is distributed in the hope that it will be useful, but WITHOUT ANY
 *    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 *    PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 *    
 *    You should have received a copy of the GNU General Public License along with
 *    Corblivar.  If not, see <http://www.gnu.org/licenses/>.
 *
 * =====================================================================================
 */

// own Corblivar header
#include "LayoutOperations.hpp"
// required Corblivar headers
#include "Math.hpp"
#include "CorblivarCore.hpp"
#include "CorblivarAlignmentReq.hpp"
#include "Block.hpp"
#include "Net.hpp"

/// memory allocation
constexpr int LayoutOperations::OP_SWAP_BLOCKS;
/// memory allocation
constexpr int LayoutOperations::OP_SWAP_BLOCKS_ENFORCE;
/// memory allocation
constexpr int LayoutOperations::OP_MOVE_TUPLE;

bool LayoutOperations::performLayoutOp(CorblivarCore& corb, int const& layout_fit_counter, bool const& SA_phase_two, bool const& revertLastOp, bool const& cooling_phase_three) {
	int op;
	int die1, tuple1, die2, tuple2, juncts;
	bool ret;

	if (LayoutOperations::DBG) {
		std::cout << "-> LayoutOperations::performLayoutOp(" << &corb << ", " << layout_fit_counter << ", " << SA_phase_two << ", " << revertLastOp << ", " << cooling_phase_three << ")" << std::endl;
	}

	// init layout operation variables
	op = -1;
	die1 = tuple1 = die2 = tuple2 = juncts = -1;

	// revert last op
	if (revertLastOp) {
		op = this->last_op;
	}
	// perform new op
	else {
		// special scenario:
		//
		// to enable guided block alignment during phase II, we dedicatedly handle
		// particular blocks of failing requests / the requests themselves;
		// however, this should only be considered for cooling phase 3, i.e., when
		// some local minima is reached; otherwise, when these operations are
		// applied too often, the cost function will largely vary in value and is
		// thus not an appropriate measure for guided minimization anymore
		//
		// note that the related handlers below do only return true in case any
		// failed alignment exists
		if (SA_phase_two && this->parameters.opt_alignment && cooling_phase_three) {

			// randomly decide to try either block swapping or swapping
			// coordinates of flexible alignments
			//
			if (Math::randB()) {

				// try to setup swapping failed blocks
				if (this->prepareBlockSwappingFailedAlignment(corb, die1, tuple1, die2, tuple2)) {

					// engage swap operation; this dedicated _ENFORCE
					// op-code ignores power-aware block-die
					// assignments, such that block alignments can be
					// also fulfilled when the option power-aware
					// block assignment is activated
					this->last_op = op = LayoutOperations::OP_SWAP_BLOCKS_ENFORCE;
				}
			}
			// other order of trying operations
			else {

				// try to setup swapping alignment coordinates
				if (this->prepareSwappingCoordinatesFailedAlignment(corb, tuple1)) {

					this->last_op = op = LayoutOperations::OP_SWAP_ALIGNMENT_COORDINATES;
				}
			}
		}
		// another scenario: in case very few valid layout were found in the
		// previous iteration, randomly select one block which currently exceeds
		// the fixed outline
		else if (Math::looseDoubleComp(layout_fit_counter, 0.0)) {

			if (Math::randB()) {
				this->prepareHandlingOutlineCriticalBlock(corb, die1, tuple1);

				// perform any random operation on that block
				this->last_op = op = Math::randI(1, 6);
			}
		}

		// for other (regular) cases or if special scenarios above cannot be
		// performed or if they still require random operations, define a random
		// operation next
		//
		if (op == -1) {

			// randomly select one block from currently largest net w/ highest
			// individual impact on WL
			if (Math::randB()) {
				this->preselectBlockFromLargestNet(corb, die1, tuple1);
			}

			// see defined op-codes to set random-number ranges; recall that
			// randI(x,y) is [x,y)
			this->last_op = op = Math::randI(1, 6);
		}
	}

	// specific op handler
	switch (op) {

		case LayoutOperations::OP_SWAP_BLOCKS: // op-code: 1

			ret = this->performOpMoveOrSwapBlocks(LayoutOperations::OP_SWAP_BLOCKS, revertLastOp, !SA_phase_two, corb, die1, die2, tuple1, tuple2);

			break;

		case LayoutOperations::OP_MOVE_TUPLE: // op-code: 2

			ret = this->performOpMoveOrSwapBlocks(LayoutOperations::OP_MOVE_TUPLE, revertLastOp, !SA_phase_two, corb, die1, die2, tuple1, tuple2);

			break;

		case LayoutOperations::OP_SWITCH_INSERTION_DIR: // op-code: 3

			ret = this->performOpSwitchInsertionDirection(revertLastOp, corb, die1, tuple1);

			break;

		case LayoutOperations::OP_SWITCH_TUPLE_JUNCTS: // op-code: 4

			ret = this->performOpSwitchTupleJunctions(revertLastOp, corb, die1, tuple1, juncts);

			break;

		case LayoutOperations::OP_ROTATE_BLOCK__SHAPE_BLOCK: // op-code: 5

			ret = this->performOpShapeBlock(revertLastOp, corb, die1, tuple1);

			break;

		case LayoutOperations::OP_SWAP_BLOCKS_ENFORCE: // op-code: 20

			ret = this->performOpMoveOrSwapBlocks(LayoutOperations::OP_SWAP_BLOCKS_ENFORCE, revertLastOp, !SA_phase_two, corb, die1, die2, tuple1, tuple2);

			break;

		case LayoutOperations::OP_SWAP_ALIGNMENT_COORDINATES: // op-code: 21

			ret = this->performOpSwapAlignmentCoordinates(revertLastOp, corb, tuple1);

			break;
	}

	// memorize elements of successful op
	if (ret) {
		this->last_op_die1 = die1;
		this->last_op_die2 = die2;
		this->last_op_tuple1 = tuple1;
		this->last_op_tuple2 = tuple2;
		this->last_op_juncts = juncts;
	}

	if (LayoutOperations::DBG) {
		std::cout << "<- LayoutOperations::performLayoutOp : " << ret << std::endl;
	}

	return ret;
}

void LayoutOperations::prepareHandlingOutlineCriticalBlock(CorblivarCore const& corb, int& die1, int& tuple1) const {
	int random_tuple;

	tuple1 = -1;

	// randomly decide whether to work on the x- or y-dimension; this part is
	// for x-direction
	if (Math::randB()) {

		// search for one critical block among all dies
		for (int l = 0; l < this->parameters.layers; l++) {

			for (unsigned b = 0; b < corb.getDie(l).getBlocks().size(); b++) {

				// randomly consider any block on the current die;
				// when it's exceeding the outline it's to be
				// altered
				random_tuple = Math::randI(0, corb.getDie(l).getBlocks().size());

				// current block exceeding die width?
				if (corb.getDie(l).getBlock(random_tuple)->bb.ur.x > this->parameters.outline.x) {
					die1 = l;
					tuple1 = random_tuple;
					break;
				}
			}

			if (tuple1 != -1) {
				break;
			}
		}
	}
	// randomly decide whether to work on the x- or y-dimension; this part is for
	// y-direction
	else {
		// search for one critical block among all dies
		for (int l = 0; l < this->parameters.layers; l++) {

			for (unsigned b = 0; b < corb.getDie(l).getBlocks().size(); b++) {

				// randomly consider any block on the current die;
				// when it's exceeding the outline it's to be
				// altered
				random_tuple = Math::randI(0, corb.getDie(l).getBlocks().size());

				// current block exceeding die height?
				if (corb.getDie(l).getBlock(random_tuple)->bb.ur.y > this->parameters.outline.y) {
					die1 = l;
					tuple1 = random_tuple;
					break;
				}
			}

			if (tuple1 != -1) {
				break;
			}
		}
	}
}

void LayoutOperations::preselectBlockFromLargestNet(CorblivarCore const& corb, int& die1, int& tuple1) const {

	// sanity check for largest net
	if (this->parameters.largest_net == nullptr) {
		return;
	}

	// randomly select one block from the largest net
	Block const* block = this->parameters.largest_net->blocks[Math::randI(0, this->parameters.largest_net->blocks.size())];

	if (LayoutOperations::DBG) {
		std::cout << "DBG_LAYOUT> LayoutOperations::preselectBlockFromLargestNet" << std::endl;
		std::cout << "DBG_LAYOUT>  Net ID: " << this->parameters.largest_net->id << std::endl;
		std::cout << "DBG_LAYOUT>  (Randomly) selected block to be altered: " << block->id << " on die " << block-> layer << std::endl;
	}

	// assign the die according to the selected block
	die1 = block->layer;

	// also determine the related tuple for the selected block
	tuple1 = corb.getDie(die1).getTuple(block);

	return;
}

bool LayoutOperations::prepareBlockSwappingFailedAlignment(CorblivarCore const& corb, int& die1, int& tuple1, int& die2, int& tuple2) {
	CorblivarAlignmentReq const* failed_req = nullptr;
	std::vector<CorblivarAlignmentReq const*> failed_reqs;
	Block const* b1;
	Block const* b1_partner;
	Block const* b1_neighbour = nullptr;
	Rect bb;

	// determine failed alignments
	for (unsigned r = 0; r < corb.getAlignments().size(); r++) {

		if (!corb.getAlignments()[r].fulfilled) {
			failed_reqs.push_back(&corb.getAlignments()[r]);
		}
	}

	// randomly pick any failed alignment
	if (!failed_reqs.empty()) {

		failed_req = failed_reqs[Math::randI(0, failed_reqs.size())];

		// randomly decide for one block to move around / to swap with other
		// blocks; avoid the dummy reference block if required
		if (
			// randomly select s_i if it's not the RBOD
			(failed_req->s_i->numerical_id != RBOD::NUMERICAL_ID && Math::randB()) ||
			// also consider s_i if s_j is the RBOD
			failed_req->s_j->numerical_id == RBOD::NUMERICAL_ID
		   ) {
			// sanity check for both s_i and s_j being RBOD
			if (failed_req->s_i->numerical_id == RBOD::NUMERICAL_ID) {
				return false;
			}

			// s_i is the block to be changed
			b1 = failed_req->s_i;
			// memorize the opposite block s_j
			b1_partner = failed_req->s_j;
		}
		else {
			// s_j is the block to be changed
			b1 = failed_req->s_j;
			// memorize the opposite block s_i
			b1_partner = failed_req->s_i;
		}

		// determine die and tuple variables; tuple2 is to be determined below
		//
		die1 = b1->layer;
		tuple1 = corb.getDie(die1).getTuple(b1);
		// for RBOD being the partner, we assume the same die as for the block to
		// be changed
		if (b1_partner->numerical_id == RBOD::NUMERICAL_ID) {
			die2 = die1;
		}
		else {
			die2 = b1_partner->layer;
		}

		if (CorblivarAlignmentReq::DBG_HANDLE_FAILED) {
			std::cout << "DBG_ALIGNMENT> s_i: " << failed_req->s_i->id << std::endl;
			std::cout << "DBG_ALIGNMENT> s_j: " << failed_req->s_j->id << std::endl;
			std::cout << "DBG_ALIGNMENT> b1: " << b1->id << std::endl;
			std::cout << "DBG_ALIGNMENT> b1_partner: " << b1_partner->id << std::endl;
			std::cout << "DBG_ALIGNMENT> die1: " << die1 << std::endl;
			std::cout << "DBG_ALIGNMENT> tuple1: " << tuple1 << std::endl;
			std::cout << "DBG_ALIGNMENT> die2: " << die2 << std::endl;
			std::cout << "DBG_ALIGNMENT> tuple1: to be determined" << std::endl;
		}

		// dedicatedly defined vertical bus; failed vertical alignment across
		// different dies
		if (failed_req->vertical_bus()) {

			// select block to swap with b1 such that blocks to be aligned (b1
			// and its partner) are initially at least intersecting blocks;
			// that means, we need to swap with a block that is intersecting
			// with b1's partner block
			//
			// if b1 and its partner block are in one die, b1 needs to be
			// swapped with a block on another layer which is intersecting
			// b1's partner block; randomly select another layer
			//
			if (die1 == die2) {

				// such vertical alignment  is only possible for > 1
				// layers; sanity check
				if (this->parameters.layers == 1) {
					return false;
				}

				while (die1 == die2) {
					die2 = Math::randI(0, this->parameters.layers);
				}
			}
			// if b1 and its partner block are in different dies, b1 can be
			// swapped with a block intersecting b1's partner block on the
			// current die of b1
			else {
				die2 = die1;
			}

			// the block to swap w/ is stepwise searched according to this bb;
			// init it with the bb of b1's partner block
			bb = b1_partner->bb;

			while (true) {

				for (Block const* b2 : corb.getDie(die2).getBlocks()) {

					// candidate block; overlaps with b1's partner
					// block
					if (Rect::rectsIntersect(bb, b2->bb) &&
						// avoid swapping with b1 itself
						b1->numerical_id != b2->numerical_id &&
						// also check that blocks are not partner blocks
						// of the alignment request; otherwise,
						// consecutively circular swap might occur which
						// are not resolving the failing alignment
						!failed_req->partner_blocks(b1, b2)
					   ) {

						b1_neighbour = b2;

						break;
					}
				}

				// no intersecting block was found, increase the search
				// radius by doubling the considered bb
				if (b1_neighbour == nullptr) {

					bb.ll.x -= bb.w / 2.0;
					bb.ur.x += bb.w / 2.0;
					bb.ll.y -= bb.h / 2.0;
					bb.ur.y += bb.h / 2.0;
				}
				else {
					break;
				}
			}

		}

		// other failed alignment ranges or non-zero-offset fixed alignment
		//
		// determine relevant neighbour block to perform swap operation, i.e.,
		// nearest neighbour w.r.t. failure type
		else {

			// also consider to randomly change die2 as well; this is required
			// for alignments which cannot be fulfilled within one die and
			// does not harm for alignments which could be fulfilled within
			// one die (they can then also be fulfilled across dies); note
			// that an explicit check for all the different options of
			// alignments not possible within one die are not performed here
			// but rather a die change is considered randomly
			//
			// note that changing dies is only possible for > 1 layers
			if (Math::randB() && this->parameters.layers > 1) {

				while (die1 == die2) {
					die2 = Math::randI(0, this->parameters.layers);
				}
			}

			// consider different neighbours for different alignment failures
			switch (b1->alignment) {

				// determine nearest right block
				case Block::AlignmentStatus::FAIL_HOR_TOO_LEFT:
		
					for (Block const* b2 : corb.getDie(die2).getBlocks()) {

						if (Rect::rectA_leftOf_rectB(b1->bb, b2->bb, true) &&
							// also check that blocks are not partner blocks
							// of the alignment request; otherwise,
							// consecutively circular swap might occur which
							// are not resolving the failing alignment
							!failed_req->partner_blocks(b1, b2)
						   ) {

							if (b1_neighbour == nullptr || b2->bb.ll.x < b1_neighbour->bb.ll.x) {
								b1_neighbour = b2;
							}
						}
					}

					break;

				// determine nearest left block
				case Block::AlignmentStatus::FAIL_HOR_TOO_RIGHT:
		
					for (Block const* b2 : corb.getDie(die2).getBlocks()) {

						if (Rect::rectA_leftOf_rectB(b2->bb, b1->bb, true) &&
							// also check that blocks are not partner blocks
							// of the alignment request; otherwise,
							// consecutively circular swap might occur which
							// are not resolving the failing alignment
							!failed_req->partner_blocks(b1, b2)
						   ) {

							if (b1_neighbour == nullptr || b2->bb.ur.x > b1_neighbour->bb.ur.x) {
								b1_neighbour = b2;
							}
						}
					}

					break;

				// determine nearest block above
				case Block::AlignmentStatus::FAIL_VERT_TOO_LOW:
		
					for (Block const* b2 : corb.getDie(die2).getBlocks()) {

						if (Rect::rectA_below_rectB(b1->bb, b2->bb, true) &&
							// also check that blocks are not partner blocks
							// of the alignment request; otherwise,
							// consecutively circular swap might occur which
							// are not resolving the failing alignment
							!failed_req->partner_blocks(b1, b2)
						   ) {

							if (b1_neighbour == nullptr || b2->bb.ll.y < b1_neighbour->bb.ll.y) {
								b1_neighbour = b2;
							}
						}
					}

					break;

				// determine nearest block below
				case Block::AlignmentStatus::FAIL_VERT_TOO_HIGH:
		
					for (Block const* b2 : corb.getDie(die2).getBlocks()) {

						if (Rect::rectA_below_rectB(b2->bb, b1->bb, true) &&
							// also check that blocks are not partner blocks
							// of the alignment request; otherwise,
							// consecutively circular swap might occur which
							// are not resolving the failing alignment
							!failed_req->partner_blocks(b1, b2)
						   ) {

							if (b1_neighbour == nullptr || b2->bb.ur.y > b1_neighbour->bb.ur.y) {
								b1_neighbour = b2;
							}
						}
					}

					break;

				// dummy case, to catch other (here not occurring)
				// alignment status
				default:
					break;
			}
		}

		// determine related tuple of neighbour block; == -1 in case the tuple
		// cannot be find; sanity check for undefined neighbour
		if (b1_neighbour != nullptr) {

			tuple2 = corb.getDie(die2).getTuple(b1_neighbour);

			if (CorblivarAlignmentReq::DBG_HANDLE_FAILED) {
				std::cout << "DBG_ALIGNMENT> " << failed_req->tupleString() << " failed so far;" << std::endl;
				std::cout << "DBG_ALIGNMENT>  failed alignment status (" << b1->id << "): " << b1->alignment << std::endl;
				std::cout << "DBG_ALIGNMENT> considering swapping block " << b1->id << " on layer " << b1->layer;
				std::cout << " with block " << b1_neighbour->id << " on layer " << b1_neighbour->layer << std::endl;
			}

			return true;
		}
		else {
			tuple2 = -1;

			if (CorblivarAlignmentReq::DBG_HANDLE_FAILED) {
				std::cout << "DBG_ALIGNMENT> " << failed_req->tupleString() << " failed so far;" << std::endl;
				std::cout << "DBG_ALIGNMENT>  failed alignment status (" << b1->id << "): " << b1->alignment << std::endl;
				std::cout << "DBG_ALIGNMENT> no appropriate block to swap with found" << std::endl;
			}

			return false;
		}
	}

	// no failing request was found
	else {
		return false;
	}
}

bool LayoutOperations::performOpEnhancedSoftBlockShaping(CorblivarCore const& corb, Block const* shape_block) const {
	int op;
	double boundary_x, boundary_y;
	double width, height;

	// see defined op-codes in class FloorPlanner to set random-number ranges;
	// recall that randI(x,y) is [x,y)
	op = Math::randI(10, 15);

	switch (op) {

		// stretch such that shape_block's right front aligns w/ the right front
		// of the nearest other block
		case LayoutOperations::OP_SHAPE_BLOCK__STRETCH_HORIZONTAL: // op-code: 10

			// dummy value, to be large than right front
			boundary_x = 2.0 * shape_block->bb.ur.x;

			for (Block const* b : corb.getDie(shape_block->layer).getBlocks()) {

				// determine nearest right front of other blocks
				if (b->bb.ur.x > shape_block->bb.ur.x) {
					boundary_x = std::min(boundary_x, b->bb.ur.x);
				}
			}

			// determine resulting new dimensions
			width = boundary_x - shape_block->bb.ll.x;
			height = shape_block->bb.area / width;

			// apply new dimensions in case the resulting AR is allowed
			return shape_block->shapeByWidthHeight(width, height);

		// shrink such that shape_block's right front aligns w/ the left front of
		// the nearest other block
		case LayoutOperations::OP_SHAPE_BLOCK__SHRINK_HORIZONTAL: // op-code: 12

			boundary_x = 0.0;

			for (Block const* b : corb.getDie(shape_block->layer).getBlocks()) {

				// determine nearest left front of other blocks
				if (b->bb.ll.x < shape_block->bb.ur.x) {
					boundary_x = std::max(boundary_x, b->bb.ll.x);
				}
			}

			// determine resulting new dimensions
			width = boundary_x - shape_block->bb.ll.x;
			height = shape_block->bb.area / width;

			// apply new dimensions in case the resulting AR is allowed
			return shape_block->shapeByWidthHeight(width, height);

		// stretch such that shape_block's top front aligns w/ the top front of
		// the nearest other block
		case LayoutOperations::OP_SHAPE_BLOCK__STRETCH_VERTICAL: // op-code: 11

			// dummy value, to be large than top front
			boundary_y = 2.0 * shape_block->bb.ur.y;

			for (Block const* b : corb.getDie(shape_block->layer).getBlocks()) {

				// determine nearest top front of other blocks
				if (b->bb.ur.y > shape_block->bb.ur.y) {
					boundary_y = std::min(boundary_y, b->bb.ur.y);
				}
			}

			// determine resulting new dimensions
			height = boundary_y - shape_block->bb.ll.y;
			width = shape_block->bb.area / height;

			// apply new dimensions in case the resulting AR is allowed
			return shape_block->shapeByWidthHeight(width, height);

		// shrink such that shape_block's top front aligns w/ the bottom front of
		// the nearest other block
		case LayoutOperations::OP_SHAPE_BLOCK__SHRINK_VERTICAL: // op-code: 13

			boundary_y = 0.0;

			for (Block const* b : corb.getDie(shape_block->layer).getBlocks()) {

				// determine nearest bottom front of other blocks
				if (b->bb.ll.y < shape_block->bb.ur.y) {
					boundary_y = std::max(boundary_y, b->bb.ll.y);
				}
			}

			// determine resulting new dimensions
			height = boundary_y - shape_block->bb.ll.y;
			width = shape_block->bb.area / height;

			// apply new dimensions in case the resulting AR is allowed
			return shape_block->shapeByWidthHeight(width, height);

		case LayoutOperations::OP_SHAPE_BLOCK__RANDOM_AR: // op-code: 14

			return shape_block->shapeRandomlyByAR();

		// to avoid compiler warnings, non-reachable code due to
		// constrained op value
		default:
			return false;
	}
}

bool LayoutOperations::performOpEnhancedHardBlockRotation(CorblivarCore const& corb, Block const* shape_block) const {
	double col_max_width, row_max_height;
	double gain, loss;

	// horizontal block
	if (shape_block->bb.w > shape_block->bb.h) {

		// check blocks in (implicitly constructed) row
		row_max_height = shape_block->bb.h;

		for (Block const* b : corb.getDie(shape_block->layer).getBlocks()) {

			if (shape_block->bb.ll.y == b->bb.ll.y) {
				row_max_height = std::max(row_max_height, b->bb.h);
			}
		}

		// gain in horizontal direction by rotation
		gain = shape_block->bb.w - shape_block->bb.h;
		// loss in vertical direction; only if new block
		// height (current width) would be larger than the
		// row's current height
		loss = shape_block->bb.w - row_max_height;
	}
	// vertical block
	else {
		// check blocks in (implicitly constructed) column
		col_max_width = shape_block->bb.w;

		for (Block const* b : corb.getDie(shape_block->layer).getBlocks()) {

			if (shape_block->bb.ll.x == b->bb.ll.x) {
				col_max_width = std::max(col_max_width, b->bb.w);
			}
		}

		// gain in vertical direction by rotation
		gain = shape_block->bb.h - shape_block->bb.w;
		// loss in horizontal direction; only if new block
		// width (current height) would be larger than the
		// column's current width
		loss = shape_block->bb.h - col_max_width;
	}

	// perform rotation if no loss or gain > loss
	if (loss < 0.0 || gain > loss) {
		return shape_block->rotate();
	}
	else {
		return false;
	}
}


bool LayoutOperations::performOpSwitchTupleJunctions(bool const& revert, CorblivarCore& corb, int& die1, int& tuple1, int& juncts) const {
	int new_juncts;

	if (!revert) {

		// randomly select die, if not preassigned
		if (die1 == -1) {
			die1 = Math::randI(0, this->parameters.layers);
		}

		// sanity check for empty dies
		if (corb.getDie(die1).getCBL().empty()) {
			return false;
		}

		// randomly select tuple, if not preassigned
		if (tuple1 == -1) {
			tuple1 = Math::randI(0, corb.getDie(die1).getCBL().size());
		}

		// juncts is for return-by-reference, new_juncts for updating junctions
		new_juncts = juncts = corb.getDie(die1).getJunctions(tuple1);

		// junctions must be geq 0
		if (new_juncts == 0) {
			new_juncts++;
		}
		else {
			if (Math::randB()) {
				new_juncts++;
			}
			else {
				new_juncts--;
			}
		}

		if (LayoutOperations::DBG) {
			std::cout << "DBG_LAYOUT> LayoutOperations::OP_SWITCH_TUPLE_JUNCTS; revert: " << revert <<
				"; die1: " << die1 << "; tuple1: " << tuple1 << "; juncts: " << new_juncts << std::endl;
		}

		corb.switchTupleJunctions(die1, tuple1, new_juncts);
	}
	else {
		if (LayoutOperations::DBG) {
			std::cout << "DBG_LAYOUT> LayoutOperations::OP_SWITCH_TUPLE_JUNCTS; revert: " << revert <<
				"; die1: " << this->last_op_die1 << "; tuple1: " << this->last_op_tuple1 << "; juncts: " << this->last_op_juncts << std::endl;
		}

		corb.switchTupleJunctions(this->last_op_die1, this->last_op_tuple1, this->last_op_juncts);
	}

	return true;
}

bool LayoutOperations::performOpSwitchInsertionDirection(bool const& revert, CorblivarCore& corb, int& die1, int& tuple1) const {

	if (!revert) {

		// randomly select die, if not preassigned
		if (die1 == -1) {
			die1 = Math::randI(0, this->parameters.layers);
		}

		// sanity check for empty dies
		if (corb.getDie(die1).getCBL().empty()) {
			return false;
		}

		// randomly select tuple, if not preassigned
		if (tuple1 == -1) {
			tuple1 = Math::randI(0, corb.getDie(die1).getCBL().size());
		}

		if (LayoutOperations::DBG) {
			std::cout << "DBG_LAYOUT> LayoutOperations::OP_SWITCH_INSERTION_DIR; revert: " << revert <<
				"; die1: " << die1 << "; tuple1: " << tuple1 << std::endl;
		}

		corb.switchInsertionDirection(die1, tuple1);
	}
	else {
		if (LayoutOperations::DBG) {
			std::cout << "DBG_LAYOUT> LayoutOperations::OP_SWITCH_INSERTION_DIR; revert: " << revert <<
				"; die1: " << this->last_op_die1 << "; tuple1: " << this->last_op_tuple1 << std::endl;
		}

		corb.switchInsertionDirection(this->last_op_die1, this->last_op_tuple1);
	}

	return true;
}

bool LayoutOperations::performOpMoveOrSwapBlocks(int const& mode, bool const& revert, bool const& SA_phase_one, CorblivarCore& corb, int& die1, int& die2, int& tuple1, int& tuple2) const {
	Block const* b2;

	if (!revert) {

		// randomly select die, if not preassigned
		if (die1 == -1) {
			die1 = Math::randI(0, this->parameters.layers);
		}
		if (die2 == -1) {
			die2 = Math::randI(0, this->parameters.layers);
		}

		// sanity checks; move operations: check for empty (origin) die
		if (mode == LayoutOperations::OP_MOVE_TUPLE) {
			if (corb.getDie(die1).getCBL().empty()) {
				return false;
			}
		}
		// sanity checks; swap operations: check for empty dies
		else {
			// sanity check for empty dies
			if (corb.getDie(die1).getCBL().empty() || corb.getDie(die2).getCBL().empty()) {
				return false;
			}
		}

		// randomly select tuple, if not preassigned
		if (tuple1 == -1) {
			tuple1 = Math::randI(0, corb.getDie(die1).getCBL().size());
		}
		if (tuple2 == -1) {
			tuple2 = Math::randI(0, corb.getDie(die2).getCBL().size());
		}

		// in case of swapping/moving w/in same die, ensure that tuples are
		// different
		if (die1 == die2) {
			// this is, however, only possible if at least two
			// tuples are given in that die
			if (corb.getDie(die1).getCBL().size() < 2) {
				return false;
			}
			// determine two different tuples
			while (tuple1 == tuple2) {
				tuple2 = Math::randI(0, corb.getDie(die1).getCBL().size());
			}
		}

		// dbg output for operation
		if (LayoutOperations::DBG) {
			if (mode == LayoutOperations::OP_MOVE_TUPLE) {
				std::cout << "DBG_LAYOUT> LayoutOperations::OP_MOVE_TUPLE;";
			}
			else if (mode == LayoutOperations::OP_SWAP_BLOCKS) {
				std::cout << "DBG_LAYOUT> LayoutOperations::OP_SWAP_BLOCKS;";
			}
			else if (mode == LayoutOperations::OP_SWAP_BLOCKS_ENFORCE) {
				std::cout << "DBG_LAYOUT> LayoutOperations::OP_SWAP_BLOCKS_ENFORCE;";
			}

			std::cout << " revert: 0;";
			std::cout << " SA_phase_one: " << SA_phase_one;
			std::cout << "; die1: " << die1 << "; die2: " << die2 << "; tuple1: " << tuple1 << "; tuple2: " << tuple2 << std::endl;
		}

		// improve alignment optimization; in case the block from die1 is
		// associated with some vertical bus, ensure that these bus' blocks are
		// not within one die afterwards
		if (this->parameters.opt_alignment) {

			for (CorblivarAlignmentReq const* req : corb.getDie(die1).getBlock(tuple1)->alignments_vertical_bus) {

				if (req->s_i->numerical_id == corb.getDie(die1).getBlock(tuple1)->numerical_id) {
					b2 = req->s_j;
				}
				else {
					b2 = req->s_i;
				}

				// if the target die die2 is the same as of the
				// alignment's partner block b2, prohibit this operation
				if (die2 == b2->layer) {

					if (LayoutOperations::DBG) {
						std::cout << "DBG_LAYOUT>  Alignment-aware block handling; operation not allowed" << std::endl;
						std::cout << "DBG_LAYOUT>   Related alignment: " << req->tupleString() << std::endl;
					}

					return false;
				}
			}
		}

		// for power-aware block handling, ensure that blocks w/ higher power
		// density remain in upper layer
		if (this->parameters.power_aware_block_handling) {
	
			// if the higher-power block is in the upper layer d1, both swaps
			// and moves from the upper layer d1 down to the lower layer d2
			// should be prohibited
			if (die1 > die2	&& (corb.getDie(die1).getBlock(tuple1)->power_density() > corb.getDie(die2).getBlock(tuple2)->power_density())
					// but for OP_SWAP_BLOCKS_ENFORCE (which is used
					// for handling failed alignments) they should be
					// considered
					&& mode != LayoutOperations::OP_SWAP_BLOCKS_ENFORCE) {

				if (LayoutOperations::DBG) {
					std::cout << "DBG_LAYOUT>  Power-aware block handling; operation not allowed" << std::endl;
					std::cout << "DBG_LAYOUT>   b1: " << corb.getDie(die1).getBlock(tuple1)->power_density() <<
						"; b2: " << corb.getDie(die2).getBlock(tuple2)->power_density() << std::endl;
				}

				return false;
			}
			// if the higher-power block is in the upper layer d2, the same
			// applies
			else if (die2 > die1 && (corb.getDie(die2).getBlock(tuple2)->power_density() > corb.getDie(die1).getBlock(tuple1)->power_density())
					// but for OP_SWAP_BLOCKS_ENFORCE (which is used
					// for handling failed alignments) they should be
					// considered
					&& mode != LayoutOperations::OP_SWAP_BLOCKS_ENFORCE) {

				if (LayoutOperations::DBG) {
					std::cout << "DBG_LAYOUT>  Power-aware block handling; operation not allowed" << std::endl;
					std::cout << "DBG_LAYOUT>   b2: " << corb.getDie(die2).getBlock(tuple2)->power_density() <<
						"; b1: " << corb.getDie(die1).getBlock(tuple1)->power_density() << std::endl;
				}

				return false;
			}
		}

		// for SA phase one, floorplacement blocks, i.e., large macros, should not
		// be moved/swapped
		if (this->parameters.floorplacement && SA_phase_one
				&& (corb.getDie(die1).getBlock(tuple1)->floorplacement || corb.getDie(die2).getBlock(tuple2)->floorplacement)) {
			return false;
		}

		// perform actual move or swap operation; applies only to valid candidates
		if (mode == LayoutOperations::OP_MOVE_TUPLE) {
			corb.moveTuples(die1, die2, tuple1, tuple2);
		}
		else {
			corb.swapBlocks(die1, die2, tuple1, tuple2);
		}
	}

	// revert last operation
	else {
		// offsets may have to be adapted for moves within one die
		//
		if (mode == LayoutOperations::OP_MOVE_TUPLE && this->last_op_die1 == this->last_op_die2) {

			// previous move: origin offset was greater than target offset;
			// thus, the tuple was moved before the origin offset, and the
			// origin offset has to increased by one
			//
			// note that, if last_op_tuple1 was the last element in the
			// underlying vector, the index will then refer to the
			// vector::end, which does not trigger memory errors and is also
			// the correct index
			if (this->last_op_tuple1 > this->last_op_tuple2) {
				this->last_op_tuple1++;
			}
			// previous move: origin offset was less than target offset; thus,
			// the target offset has to be decreased by one to account for the
			// removed tuple
			//
			// note that, since tuple1 != tuple2 by previously defined
			// original move operation for within one die, and since tuple1 <
			// tuple2 due to previous reason and above case handling (tuple1 >
			// tuple2), tuple2 is guaranteed to meet tuple2 > 1. This avoids
			// tuple2-- to be < 0 and thus avoids memory access errors
			else {
				this->last_op_tuple2--;
			}
		}

		// dbg output for operation
		if (LayoutOperations::DBG) {
			if (mode == LayoutOperations::OP_MOVE_TUPLE) {
				std::cout << "DBG_LAYOUT> LayoutOperations::OP_MOVE_TUPLE;";
			}
			else if (mode == LayoutOperations::OP_SWAP_BLOCKS) {
				std::cout << "DBG_LAYOUT> LayoutOperations::OP_SWAP_BLOCKS;";
			}
			else if (mode == LayoutOperations::OP_SWAP_BLOCKS_ENFORCE) {
				std::cout << "DBG_LAYOUT> LayoutOperations::OP_SWAP_BLOCKS_ENFORCE;";
			}

			std::cout << " revert: 1;";
			std::cout << " SA_phase_one: " << SA_phase_one;
			std::cout << "; die1: " << this->last_op_die1 << "; die2: " << this->last_op_die2 << "; tuple1: " << this->last_op_tuple1 << "; tuple2: " << this->last_op_tuple2;
			std::cout << std::endl;
		}

		// perform actual operation
		if (mode == LayoutOperations::OP_MOVE_TUPLE) {
			corb.moveTuples(this->last_op_die2, this->last_op_die1, this->last_op_tuple2, this->last_op_tuple1);
		}
		else {
			corb.swapBlocks(this->last_op_die1, this->last_op_die2, this->last_op_tuple1, this->last_op_tuple2);
		}
	}

	return true;
}

bool LayoutOperations::performOpShapeBlock(bool const& revert, CorblivarCore& corb, int& die1, int& tuple1) const {
	Block const* shape_block;

	if (!revert) {

		// randomly select die, if not preassigned
		if (die1 == -1) {
			die1 = Math::randI(0, this->parameters.layers);
		}

		// sanity check for empty dies
		if (corb.getDie(die1).getCBL().empty()) {
			return false;
		}

		// randomly select tuple, if not preassigned
		if (tuple1 == -1) {
			tuple1 = Math::randI(0, corb.getDie(die1).getCBL().size());
		}

		if (LayoutOperations::DBG) {
			std::cout << "DBG_LAYOUT> LayoutOperations::OP_ROTATE_BLOCK__SHAPE_BLOCK; revert: " << revert <<
				"; die1: " << die1 << "; tuple1: " << tuple1 << std::endl;
		}

		// determine related block to be shaped
		shape_block = corb.getDie(die1).getBlock(tuple1);

		// backup current shape
		shape_block->bb_backup = shape_block->bb;

		// soft blocks: enhanced block shaping
		if (shape_block->soft) {
			// enhanced shaping, according to [Chen06]
			if (this->parameters.enhanced_soft_block_shaping) {
				return this->performOpEnhancedSoftBlockShaping(corb, shape_block);
			}
			// simple random shaping
			else {
				return shape_block->shapeRandomlyByAR();
			}
		}
		// hard blocks: simple rotation or enhanced rotation (perform block
		// rotation only if layout compaction is achievable); note that this
		// enhanced rotation relies on non-compacted, i.e., non-packed layouts,
		// which is checked during config file parsing
		else {
			// enhanced rotation
			if (this->parameters.enhanced_hard_block_rotation) {
				return this->performOpEnhancedHardBlockRotation(corb, shape_block);
			}
			// simple rotation
			else {
				return shape_block->rotate();
			}
		}
	}
	// revert last rotation
	else {
		if (LayoutOperations::DBG) {
			std::cout << "DBG_LAYOUT> LayoutOperations::OP_ROTATE_BLOCK__SHAPE_BLOCK; revert: " << revert <<
				"; die1: " << this->last_op_die1 << "; tuple1: " << this->last_op_tuple1 << std::endl;
		}

		// revert by restoring backup bb
		corb.getDie(this->last_op_die1).getBlock(this->last_op_tuple1)->bb =
			corb.getDie(this->last_op_die1).getBlock(this->last_op_tuple1)->bb_backup;
	}

	return true;
}

bool LayoutOperations::prepareSwappingCoordinatesFailedAlignment(CorblivarCore const& corb, int& tuple1) {
	std::vector<unsigned> failed_reqs_tuple_index;

	// determine failed alignments w/ flexible alignment handling; only such flexible
	// requests allow to swap their coordinates / partial requests
	for (unsigned r = 0; r < corb.getAlignments().size(); r++) {

		if (!corb.getAlignments()[r].fulfilled && corb.getAlignments()[r].handling == CorblivarAlignmentReq::Handling::FLEXIBLE) {
			failed_reqs_tuple_index.push_back(r);
		}
	}

	// randomly pick any failed alignment; onl
	if (!failed_reqs_tuple_index.empty()) {

		tuple1 = failed_reqs_tuple_index[Math::randI(0, failed_reqs_tuple_index.size())];

		if (CorblivarAlignmentReq::DBG_HANDLE_FAILED) {
			std::cout << "DBG_ALIGNMENT> " << corb.getAlignments()[tuple1].tupleString() << " failed so far;" << std::endl;
			std::cout << "DBG_ALIGNMENT> swapping flexible partial alignments (swapping x- and y-alignment)" << std::endl;
		}

		return true;
	}

	return false;
}

bool LayoutOperations::performOpSwapAlignmentCoordinates(bool const& revert, CorblivarCore& corb, int& tuple1) const {

	if (!revert) {

		// sanity check for assigned and valid tuple
		if (tuple1 == -1 || tuple1 >= static_cast<int>(corb.getAlignments().size())) {
			return false;
		}

		if (LayoutOperations::DBG) {
			std::cout << "DBG_LAYOUT> LayoutOperations::OP_SWAP_ALIGNMENT_COORDINATES; revert: " << revert <<
				"; tuple: " << tuple1 << std::endl;
		}

		corb.swapAlignmentCoordinates(tuple1);
	}
	else {
		if (LayoutOperations::DBG) {
			std::cout << "DBG_LAYOUT> LayoutOperations::OP_SWAP_ALIGNMENT_COORDINATES; revert: " << revert <<
				"; tuple: " << this->last_op_tuple1 << std::endl;
		}

		corb.swapAlignmentCoordinates(this->last_op_tuple1);
	}

	return true;
}
