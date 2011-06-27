/*
 *  aligner_seed_policy
 */

#include <string>
#include <iostream>
#include <sstream>
#include "ds.h"
#include "aligner_seed_policy.h"
#include "mem_ids.h"

using namespace std;

/**
 * Parse alignment policy when provided in this format:
 * <lab>=<val>;<lab>=<val>;<lab>=<val>...
 *
 * And label=value possibilities are:
 *
 * Bonus for a match
 * -----------------
 *
 * MA=xx (default: MA=0, or MA=2 if --local is set)
 *
 *    xx = Each position where equal read and reference characters match up
 *         in the alignment contriubtes this amount to the total score.
 *
 * Penalty for a mismatch
 * ----------------------
 *
 * MMP={Cxx|Q|RQ} (default: MMP=C6)
 *
 *   Cxx = Each mismatch costs xx.  If MMP=Cxx is specified, quality
 *         values are ignored when assessing penalities for mismatches.
 *   Q   = Each mismatch incurs a penalty equal to the mismatched base's
 *         value.
 *   R   = Each mismatch incurs a penalty equal to the mismatched base's
 *         rounded quality value.  Qualities are rounded off to the
 *         nearest 10, and qualities greater than 30 are rounded to 30.
 *
 * Penalty for a SNP in a colorspace alignment
 * -------------------------------------------
 *
 * SNP=xx (default: SNP=6)
 *
 *    xx = Each nucleotide difference in a decoded colorspace alignment
 *         costs xx.  This should be about equal to -10 * log10(expected
 *         fraction of positions that are SNPs)
 * 
 * Penalty for position with N (in either read or reference)
 * ---------------------------------------------------------
 *
 * NP={Cxx|Q|RQ} (default: NP=C1)
 *
 *   Cxx = Each alignment position with an N in either the read or the
 *         reference costs xx.  If NP=Cxx is specified, quality values are
 *         ignored when assessing penalities for Ns.
 *   Q   = Each alignment position with an N in either the read or the
 *         reference incurs a penalty equal to the read base's quality
 *         value.
 *   R   = Each alignment position with an N in either the read or the
 *         reference incurs a penalty equal to the read base's rounded
 *         quality value.  Qualities are rounded off to the nearest 10,
 *         and qualities greater than 30 are rounded to 30.
 *
 * Penalty for a read gap
 * ----------------------
 *
 * RDG=xx,yy (default: RDG=5,3)
 *
 *   xx    = Read gap open penalty.
 *   yy    = Read gap extension penalty.
 *
 * Total cost incurred by a read gap = xx + (yy * gap length)
 *
 * Penalty for a reference gap
 * ---------------------------
 *
 * RFG=xx,yy (default: RFG=5,3)
 *
 *   xx    = Reference gap open penalty.
 *   yy    = Reference gap extension penalty.
 *
 * Total cost incurred by a reference gap = xx + (yy * gap length)
 *
 * Minimum score for valid alignment
 * ---------------------------------
 *
 * MIN=xx,yy (defaults: MIN=-0.6,-0.6, or MIN=0.0,0.66 if --local is set)
 *
 *   xx,yy = For a read of length N, the total score must be at least
 *           xx + (read length * yy) for the alignment to be valid.  The
 *           total score is the sum of all negative penalties (from
 *           mismatches and gaps) and all positive bonuses.  The minimum
 *           can be negative (and is by default in global alignment mode).
 *
 * Score floor for local alignment
 * -------------------------------
 *
 * FL=xx,yy (defaults: FL=-Infinity,0.0, or FL=0.0,0.0 if --local is set)
 *
 *   xx,yy = If a cell in the dynamic programming table has a score less
 *           than xx + (read length * yy), then no valid alignment can go
 *           through it.  Defaults are highly recommended.
 *
 * N ceiling
 * ---------
 *
 * NCEIL=xx,yy (default: NCEIL=0.0,0.15)
 *
 *   xx,yy = For a read of length N, the number of alignment
 *           positions with an N in either the read or the
 *           reference cannot exceed
 *           ceiling = xx + (read length * yy).  If the ceiling is
 *           exceeded, the alignment is considered invalid.
 *
 * Seeds
 * -----
 *
 * SEED=mm,len,ival (default: SEED=0,22)
 *
 *   mm   = Maximum number of mismatches allowed within a seed.
 *          Must be >= 0 and <= 2.  Note that 2-mismatch mode is
 *          not fully sensitive; i.e. some 2-mismatch seed
 *          alignments may be missed.
 *   len  = Length of seed.
 *   ival = Interval between seeds.  If not specified, seed
 *          interval is determined by IVAL.
 *
 * Seed interval
 * -------------
 *
 * IVAL={L|S|C},xx,yy (default: IVAL=S,1.0,0.0)
 *
 *   L  = let interval between seeds be a linear function of the
 *        read length.  xx and yy are the constant and linear
 *        coefficients respectively.  In other words, the interval
 *        equals a * len + b, where len is the read length.
 *        Intervals less than 1 are rounded up to 1.
 *   S  = let interval between seeds be a function of the sqaure
 *        root of the  read length.  xx and yy are the
 *        coefficients.  In other words, the interval equals
 *        a * sqrt(len) + b, where len is the read length.
 *        Intervals less than 1 are rounded up to 1.
 *   C  = Like S but uses cube root of length instead of square
 *        root.
 *
 * Example 1:
 *
 *  SEED=1,10,5 and read sequence is TGCTATCGTACGATCGTAC:
 *
 *  The following seeds are extracted from the forward
 *  representation of the read and aligned to the reference
 *  allowing up to 1 mismatch:
 *
 *  Read:    TGCTATCGTACGATCGTACA
 *
 *  Seed 1+: TGCTATCGTA
 *  Seed 2+:      TCGTACGATC
 *  Seed 3+:           CGATCGTACA
 *
 *  ...and the following are extracted from the reverse-complement
 *  representation of the read and align to the reference allowing
 *  up to 1 mismatch:
 *
 *  Seed 1-: TACGATAGCA
 *  Seed 2-:      GATCGTACGA
 *  Seed 3-:           TGTACGATCG
 *
 * Example 2:
 *
 *  SEED=1,20,20 and read sequence is TGCTATCGTACGATC.  The seed
 *  length is 20 but the read is only 15 characters long.  In this
 *  case, Bowtie2 automatically shrinks the seed length to be equal
 *  to the read length.
 *
 *  Read:    TGCTATCGTACGATC
 *
 *  Seed 1+: TGCTATCGTACGATC
 *  Seed 1-: GATCGTACGATAGCA
 *
 * Example 3:
 *
 *  SEED=1,10,10 and read sequence is TGCTATCGTACGATC.  Only one seed
 *  fits on the read; a second seed would overhang the end of the read
 *  by 5 positions.  In this case, Bowtie2 extracts one seed.
 *
 *  Read:    TGCTATCGTACGATC
 *
 *  Seed 1+: TGCTATCGTA
 *  Seed 1-: TACGATAGCA
 */
void SeedAlignmentPolicy::parseString(
	const  string& s,      // string to parse
	bool   local,          // do local alignment
	bool   noisyHpolymer,  // penalize gaps less for technology reasons
	int&   bonusMatchType,
	int&   bonusMatch,
	int&   penMmcType,
	int&   penMmc,
	int&   penSnp,
	int&   penNType,
	int&   penN,
	int&   penRdExConst,
	int&   penRfExConst,
	int&   penRdExLinear,
	int&   penRfExLinear,
	float& costMinConst,
	float& costMinLinear,
	float& costFloorConst,
	float& costFloorLinear,
	float& nCeilConst,
	float& nCeilLinear,
	bool&  nCatPair,
	int&   multiseedMms,
	int&   multiseedLen,
	int&   multiseedPeriod,
	int&   multiseedIvalType,
	float& multiseedIvalA,
	float& multiseedIvalB,
	float& posmin,
	float& posfrac,
	float& rowmin,
	float& rowmult)
{

	bonusMatchType    = local ? DEFAULT_MATCH_BONUS_TYPE_LOCAL :
	                            DEFAULT_MATCH_BONUS_TYPE;
	bonusMatch        = local ? DEFAULT_MATCH_BONUS_LOCAL :
	                            DEFAULT_MATCH_BONUS;
	
	penMmcType        = DEFAULT_MM_PENALTY_TYPE;
	penMmc            = DEFAULT_MM_PENALTY;
	penSnp            = DEFAULT_SNP_PENALTY;
	penNType          = DEFAULT_N_PENALTY_TYPE;
	penN              = DEFAULT_N_PENALTY;
	costMinConst      = local ? DEFAULT_MIN_CONST_LOCAL :
	                            DEFAULT_MIN_CONST;
	costMinLinear     = local ? DEFAULT_MIN_LINEAR_LOCAL :
								DEFAULT_MIN_LINEAR;
	costFloorConst    = local ? DEFAULT_FLOOR_CONST_LOCAL :
	                            DEFAULT_FLOOR_CONST;
	costFloorLinear   = local ? DEFAULT_FLOOR_LINEAR_LOCAL :
	                            DEFAULT_FLOOR_LINEAR;
	nCeilConst        = DEFAULT_N_CEIL_CONST;
	nCeilLinear       = DEFAULT_N_CEIL_LINEAR;
	nCatPair          = DEFAULT_N_CAT_PAIR;

	if(!noisyHpolymer) {
		penRdExConst  = DEFAULT_READ_GAP_CONST;
		penRdExLinear = DEFAULT_READ_GAP_LINEAR;
		penRfExConst  = DEFAULT_REF_GAP_CONST;
		penRfExLinear = DEFAULT_REF_GAP_LINEAR;
	} else {
		penRdExConst  = DEFAULT_READ_GAP_CONST_BADHPOLY;
		penRdExLinear = DEFAULT_READ_GAP_LINEAR_BADHPOLY;
		penRfExConst  = DEFAULT_REF_GAP_CONST_BADHPOLY;
		penRfExLinear = DEFAULT_REF_GAP_LINEAR_BADHPOLY;
	}
	
	multiseedMms      = DEFAULT_SEEDMMS;
	multiseedLen      = DEFAULT_SEEDLEN;
	multiseedPeriod   = DEFAULT_SEEDPERIOD;
	multiseedIvalType = DEFAULT_IVAL;
	multiseedIvalA    = DEFAULT_IVAL_A;
	multiseedIvalB    = DEFAULT_IVAL_B;
	
	posmin   = DEFAULT_POSMIN;
	posfrac  = DEFAULT_POSFRAC;
	rowmin   = DEFAULT_ROWMIN;
	rowmult  = DEFAULT_ROWMULT;

	EList<string> toks(MISC_CAT);
	string tok;
	istringstream ss(s);
	int setting = 0;
	// Get each ;-separated token
	while(getline(ss, tok, ';')) {
		setting++;
		EList<string> etoks(MISC_CAT);
		string etok;
		// Divide into tokens on either side of =
		istringstream ess(tok);
		while(getline(ess, etok, '=')) {
			etoks.push_back(etok);
		}
		// Must be exactly 1 =
		if(etoks.size() != 2) {
			cerr << "Error parsing alignment policy setting " << setting << "; must be bisected by = sign" << endl
				 << "Policy: " << s << endl;
			assert(false); throw 1;
		}
		// LHS is tag, RHS value
		string tag = etoks[0], val = etoks[1];
		// Separate value into comma-separated tokens
		EList<string> ctoks(MISC_CAT);
		string ctok;
		istringstream css(val);
		while(getline(css, ctok, ',')) {
			ctoks.push_back(ctok);
		}
		if(ctoks.size() == 0) {
			cerr << "Error parsing alignment policy setting " << setting
			     << "; RHS must have at least 1 token" << endl
				 << "Policy: " << s << endl;
			assert(false); throw 1;
		}
		// In no case is >3 tokens OK
		if(ctoks.size() > 3) {
			cerr << "Error parsing alignment policy setting " << setting
			     << "; RHS must have at most 3 tokens" << endl
				 << "Policy: " << s << endl;
			assert(false); throw 1;
		}
		for(size_t i = 0; i < ctoks.size(); i++) {
			if(ctoks[i].length() == 0) {
				cerr << "Error parsing alignment policy setting " << setting
				     << "; token " << i+1 << " on RHS had length=0" << endl
					 << "Policy: " << s << endl;
				assert(false); throw 1;
			}
		}
		// Bonus for a match
		// MA=xx (default: MA=0, or MA=10 if --local is set)
		if(tag == "MA") {
			if(ctoks.size() != 1) {
				cerr << "Error parsing alignment policy setting " << setting
				     << "; RHS must have 1 token" << endl
					 << "Policy: " << s << endl;
				assert(false); throw 1;
			}
			string tmp = ctoks[0];
			// Parse SNP penalty
			istringstream tmpss(tmp);
			tmpss >> bonusMatch;
		}
		// Penalties for SNPs in colorspace alignments
		// SNP=xx
		//        xx = penalty
		else if(tag == "SNP") {
			if(ctoks.size() != 1) {
				cerr << "Error parsing alignment policy setting " << setting
				     << "; RHS must have 1 token" << endl
					 << "Policy: " << s << endl;
				assert(false); throw 1;
			}
			string tmp = ctoks[0];
			// Parse SNP penalty
			istringstream tmpss(tmp);
			tmpss >> penSnp;
		}
		// Scoring for mismatches
		// MMP={Cxx|Q|RQ}
		//        Cxx = constant, where constant is integer xx
		//        Q   = equal to quality
		//        R   = equal to maq-rounded quality value (rounded to nearest
		//              10, can't be greater than 30)
		else if(tag == "MMP") {
			if(ctoks.size() != 1) {
				cerr << "Error parsing alignment policy setting "
				     << "'" << tag << "'"
				     << "; RHS must have 1 token" << endl
					 << "Policy: '" << s << "'" << endl;
				assert(false); throw 1;
			}
			if(ctoks[0][0] == 'C') {
				string tmp = ctoks[0].substr(1);
				// Parse constant penalty
				istringstream tmpss(tmp);
				tmpss >> penMmc;
				// Parse constant penalty
				penMmcType = COST_MODEL_CONSTANT;
			} else if(ctoks[0][0] == 'Q') {
				// Set type to =quality
				penMmcType = COST_MODEL_QUAL;
			} else if(ctoks[0][0] == 'R') {
				// Set type to=Maq-quality
				penMmcType = COST_MODEL_ROUNDED_QUAL;
			} else {
				cerr << "Error parsing alignment policy setting "
				     << "'" << tag << "'"
				     << "; RHS must start with C, Q or R" << endl
					 << "Policy: '" << s << "'" << endl;
				assert(false); throw 1;
			}
		}
		// Scoring for mismatches where read char=N
		// NP={Cxx|Q|RQ}
		//        Cxx = constant, where constant is integer xx
		//        Q   = equal to quality
		//        R   = equal to maq-rounded quality value (rounded to nearest
		//              10, can't be greater than 30)
		else if(tag == "NP") {
			if(ctoks.size() != 1) {
				cerr << "Error parsing alignment policy setting "
				     << "'" << tag << "'"
				     << "; RHS must have 1 token" << endl
					 << "Policy: '" << s << "'" << endl;
				assert(false); throw 1;
			}
			if(ctoks[0][0] == 'C') {
				string tmp = ctoks[0].substr(1);
				// Parse constant penalty
				istringstream tmpss(tmp);
				tmpss >> penN;
				// Parse constant penalty
				penNType = COST_MODEL_CONSTANT;
			} else if(ctoks[0][0] == 'Q') {
				// Set type to =quality
				penNType = COST_MODEL_QUAL;
			} else if(ctoks[0][0] == 'R') {
				// Set type to=Maq-quality
				penNType = COST_MODEL_ROUNDED_QUAL;
			} else {
				cerr << "Error parsing alignment policy setting "
				     << "'" << tag << "'"
				     << "; RHS must start with C, Q or R" << endl
					 << "Policy: '" << s << "'" << endl;
				assert(false); throw 1;
			}
		}
		// Scoring for read gaps
		// RDG=xx,yy,zz
		//        xx = read gap open penalty
		//        yy = read gap extension penalty constant coefficient
		//             (defaults to open penalty)
		//        zz = read gap extension penalty linear coefficient
		//             (defaults to 0)
		else if(tag == "RDG") {
			if(ctoks.size() >= 1) {
				istringstream tmpss(ctoks[0]);
				tmpss >> penRdExConst;
			} else {
				penRdExConst = noisyHpolymer ?
					DEFAULT_READ_GAP_CONST_BADHPOLY :
					DEFAULT_READ_GAP_CONST;
			}
			if(ctoks.size() >= 2) {
				istringstream tmpss(ctoks[1]);
				tmpss >> penRdExLinear;
			} else {
				penRdExLinear = noisyHpolymer ?
					DEFAULT_READ_GAP_LINEAR_BADHPOLY :
					DEFAULT_READ_GAP_LINEAR;
			}
		}
		// Scoring for reference gaps
		// RFG=xx,yy,zz
		//        xx = ref gap open penalty
		//        yy = ref gap extension penalty constant coefficient
		//             (defaults to open penalty)
		//        zz = ref gap extension penalty linear coefficient
		//             (defaults to 0)
		else if(tag == "RFG") {
			if(ctoks.size() >= 1) {
				istringstream tmpss(ctoks[0]);
				tmpss >> penRfExConst;
			} else {
				penRfExConst = noisyHpolymer ?
					DEFAULT_REF_GAP_CONST_BADHPOLY :
					DEFAULT_REF_GAP_CONST;
			}
			if(ctoks.size() >= 2) {
				istringstream tmpss(ctoks[1]);
				tmpss >> penRfExLinear;
			} else {
				penRfExLinear = noisyHpolymer ?
					DEFAULT_REF_GAP_LINEAR_BADHPOLY :
					DEFAULT_REF_GAP_LINEAR;
			}
		}
		// Minimum score as a function of read length
		// MIN=xx,yy
		//        xx = constant coefficient
		//        yy = linear coefficient
		else if(tag == "MIN") {
			if(ctoks.size() >= 1) {
				istringstream tmpss(ctoks[0]);
				tmpss >> costMinConst;
			}
			if(ctoks.size() >= 2) {
				istringstream tmpss(ctoks[1]);
				tmpss >> costMinLinear;
			}
		}
		// If a total of N seed positions fit onto the read, try look for seed
		// hits from xx + yy * N of them
		// POSF=xx,yy
		//        xx = always examine at least this many poss
		//        yy = examine this times N more
		else if(tag == "POSF") {
			if(ctoks.size() >= 1) {
				istringstream tmpss(ctoks[0]);
				tmpss >> posmin;
			}
			if(ctoks.size() >= 2) {
				istringstream tmpss(ctoks[1]);
				tmpss >> posfrac;
			}
		}
		// Try a maximum of N * xx seed extensions from any given seed position.
		// ROWM=xx
		//        xx = floating-point fraction
		else if(tag == "ROWM") {
			if(ctoks.size() >= 1) {
				istringstream tmpss(ctoks[0]);
				tmpss >> rowmult;
			}
			if(ctoks.size() >= 2) {
				istringstream tmpss(ctoks[1]);
				tmpss >> rowmin;
			}
		}
		// Local-alignment score floor as a function of read length
		// FL=xx,yy
		//        xx = constant coefficient
		//        yy = linear coefficient
		else if(tag == "FL") {
			if(ctoks.size() >= 1) {
				istringstream tmpss(ctoks[0]);
				tmpss >> costFloorConst;
			}
			if(ctoks.size() >= 2) {
				istringstream tmpss(ctoks[1]);
				tmpss >> costFloorLinear;
			}
		}
		// Per-read N ceiling as a function of read length
		// NCEIL=xx,yy
		//        xx = N ceiling constant coefficient
		//        yy = N ceiling linear coefficient (set to 0 if unspecified)
		else if(tag == "NCEIL") {
			if(ctoks.size() >= 1) {
				istringstream tmpss(ctoks[0]);
				tmpss >> nCeilConst;
			}
			if(ctoks.size() >= 2) {
				istringstream tmpss(ctoks[1]);
				tmpss >> nCeilLinear;
			} else {
				nCeilLinear = DEFAULT_N_CEIL_LINEAR;
			}
		}
		/*
		 * Seeds
		 * -----
		 *
		 * SEED=mm,len,ival (default: SEED=0,22)
		 *
		 *   mm   = Maximum number of mismatches allowed within a seed.
		 *          Must be >= 0 and <= 2.  Note that 2-mismatch mode is
		 *          not fully sensitive; i.e. some 2-mismatch seed
		 *          alignments may be missed.
		 *   len  = Length of seed.
		 *   ival = Interval between seeds.  If not specified, seed
		 *          interval is determined by IVAL.
		 */
		else if(tag == "SEED") {
			if(ctoks.size() >= 1) {
				istringstream tmpss(ctoks[0]);
				tmpss >> multiseedMms;
			}
			if(ctoks.size() >= 2) {
				istringstream tmpss(ctoks[1]);
				tmpss >> multiseedLen;
			} else {
				multiseedLen = DEFAULT_SEEDLEN;
			}
			if(ctoks.size() >= 3) {
				istringstream tmpss(ctoks[2]);
				tmpss >> multiseedPeriod;
			} else {
				multiseedPeriod = DEFAULT_SEEDPERIOD;
			}
		}
		/*
		 * Seed interval
		 * -------------
		 *
		 * IVAL={L|S|C},a,b (default: IVAL=S,1.0,0.0)
		 *
		 *   L  = let interval between seeds be a linear function of the
		 *        read length.  xx and yy are the constant and linear
		 *        coefficients respectively.  In other words, the interval
		 *        equals a * len + b, where len is the read length.
		 *        Intervals less than 1 are rounded up to 1.
		 *   S  = let interval between seeds be a function of the sqaure
		 *        root of the  read length.  xx and yy are the
		 *        coefficients.  In other words, the interval equals
		 *        a * sqrt(len) + b, where len is the read length.
		 *        Intervals less than 1 are rounded up to 1.
		 *   C  = Like S but uses cube root of length instead of square
		 *        root.
		 */
		else if(tag == "IVAL") {
			if(ctoks.size() >= 1) {
				if(ctoks[0][0] == 'L') {
					multiseedIvalType = SEED_IVAL_LINEAR;
				} else if(ctoks[0][0] == 'S') {
					multiseedIvalType = SEED_IVAL_SQUARE_ROOT;
				} else if(ctoks[0][0] == 'C') {
					multiseedIvalType = SEED_IVAL_CUBE_ROOT;
				}
			}
			// A = Linear coefficient
			if(ctoks.size() >= 2) {
				istringstream tmpss(ctoks[1]);
				tmpss >> multiseedIvalA;
			} else {
				multiseedIvalA = 1.0f;
			}
			// B = Constant coefficient
			if(ctoks.size() >= 3) {
				istringstream tmpss(ctoks[2]);
				tmpss >> multiseedIvalB;
			} else {
				multiseedIvalB = 0.0f;
			}
		}
		else {
			// Unknown tag
			cerr << "Unexpected alignment policy setting "
				 << "'" << tag << "'" << endl
				 << "Policy: '" << s << "'" << endl;
			assert(false); throw 1;
		}
	}
}

#ifdef ALIGNER_SEED_POLICY_MAIN
int main() {

	int bonusMatchType;
	int bonusMatch;
	int penMmcType;
	int penMmc;
	int penSnp;
	int penNType;
	int penN;
	int penRdExConst;
	int penRfExConst;
	int penRdExLinear;
	int penRfExLinear;
	float costMinConst;
	float costMinLinear;
	float costFloorConst;
	float costFloorLinear;
	float nCeilConst;
	float nCeilLinear;
	bool  nCatPair;
	int multiseedMms;
	int multiseedLen;
	int multiseedPeriod;
	int multiseedIvalType;
	float multiseedIvalA;
	float multiseedIvalB;
	float posmin;
	float posfrac;
	float rowmin;
	float rowmult;

	{
		cout << "Case 1: Defaults 1 ... ";
		const char *pol = "";
		SeedAlignmentPolicy::parseString(
			string(pol),
			false,              // --local?
			false,              // noisy homopolymers a la 454?
			bonusMatchType,
			bonusMatch,
			penMmcType,
			penMmc,
			penSnp,
			penNType,
			penN,
			penRdExConst,
			penRfExConst,
			penRdExLinear,
			penRfExLinear,
			costMinConst,
			costMinLinear,
			costFloorConst,
			costFloorLinear,
			nCeilConst,
			nCeilLinear,
			nCatPair,
			multiseedMms,
			multiseedLen,
			multiseedPeriod,
			multiseedIvalType,
			multiseedIvalA,
			multiseedIvalB,
			posmin,
			posfrac,
			rowmin,
			rowmult
		);
		
		assert_eq(DEFAULT_MATCH_BONUS_TYPE,   bonusMatchType);
		assert_eq(DEFAULT_MATCH_BONUS,        bonusMatch);
		assert_eq(DEFAULT_MM_PENALTY_TYPE,    penMmcType);
		assert_eq(DEFAULT_MM_PENALTY,         penMmc);
		assert_eq(DEFAULT_SNP_PENALTY,        penSnp);
		assert_eq(DEFAULT_N_PENALTY_TYPE,     penNType);
		assert_eq(DEFAULT_N_PENALTY,          penN);
		assert_eq(DEFAULT_MIN_CONST,          costMinConst);
		assert_eq(DEFAULT_MIN_LINEAR,         costMinLinear);
		assert_eq(DEFAULT_FLOOR_CONST,        costFloorConst);
		assert_eq(DEFAULT_FLOOR_LINEAR,       costFloorLinear);
		assert_eq(DEFAULT_N_CEIL_CONST,       nCeilConst);
		assert_eq(DEFAULT_N_CEIL_LINEAR,      nCeilLinear);
		assert_eq(DEFAULT_N_CAT_PAIR,         nCatPair);

		assert_eq(DEFAULT_READ_GAP_CONST,     penRdExConst);
		assert_eq(DEFAULT_READ_GAP_LINEAR,    penRdExLinear);
		assert_eq(DEFAULT_REF_GAP_CONST,      penRfExConst);
		assert_eq(DEFAULT_REF_GAP_LINEAR,     penRfExLinear);
		assert_eq(DEFAULT_SEEDMMS,            multiseedMms);
		assert_eq(DEFAULT_SEEDLEN,            multiseedLen);
		assert_eq(DEFAULT_SEEDPERIOD,         multiseedPeriod);
		assert_eq(DEFAULT_IVAL,               multiseedIvalType);
		assert_eq(DEFAULT_IVAL_A,             multiseedIvalA);
		assert_eq(DEFAULT_IVAL_B,             multiseedIvalB);

		assert_eq(DEFAULT_POSMIN,             posmin);
		assert_eq(DEFAULT_POSFRAC,            posfrac);
		assert_eq(DEFAULT_ROWMIN,             rowmin);
		assert_eq(DEFAULT_ROWMULT,            rowmult);
		
		cout << "PASSED" << endl;
	}

	{
		cout << "Case 2: Defaults 2 ... ";
		const char *pol = "";
		SeedAlignmentPolicy::parseString(
			string(pol),
			false,             // --local?
			true,              // noisy homopolymers a la 454?
			bonusMatchType,
			bonusMatch,
			penMmcType,
			penMmc,
			penSnp,
			penNType,
			penN,
			penRdExConst,
			penRfExConst,
			penRdExLinear,
			penRfExLinear,
			costMinConst,
			costMinLinear,
			costFloorConst,
			costFloorLinear,
			nCeilConst,
			nCeilLinear,
			nCatPair,
			multiseedMms,
			multiseedLen,
			multiseedPeriod,
			multiseedIvalType,
			multiseedIvalA,
			multiseedIvalB,
			posmin,
			posfrac,
			rowmin,
			rowmult
		);
		
		assert_eq(DEFAULT_MATCH_BONUS_TYPE,   bonusMatchType);
		assert_eq(DEFAULT_MATCH_BONUS,        bonusMatch);
		assert_eq(DEFAULT_MM_PENALTY_TYPE,    penMmcType);
		assert_eq(DEFAULT_MM_PENALTY,         penMmc);
		assert_eq(DEFAULT_SNP_PENALTY,        penSnp);
		assert_eq(DEFAULT_N_PENALTY_TYPE,     penNType);
		assert_eq(DEFAULT_N_PENALTY,          penN);
		assert_eq(DEFAULT_MIN_CONST,          costMinConst);
		assert_eq(DEFAULT_MIN_LINEAR,         costMinLinear);
		assert_eq(DEFAULT_FLOOR_CONST,        costFloorConst);
		assert_eq(DEFAULT_FLOOR_LINEAR,       costFloorLinear);
		assert_eq(DEFAULT_N_CEIL_CONST,       nCeilConst);
		assert_eq(DEFAULT_N_CEIL_LINEAR,      nCeilLinear);
		assert_eq(DEFAULT_N_CAT_PAIR,         nCatPair);

		assert_eq(DEFAULT_READ_GAP_CONST_BADHPOLY,  penRdExConst);
		assert_eq(DEFAULT_READ_GAP_LINEAR_BADHPOLY, penRdExLinear);
		assert_eq(DEFAULT_REF_GAP_CONST_BADHPOLY,   penRfExConst);
		assert_eq(DEFAULT_REF_GAP_LINEAR_BADHPOLY,  penRfExLinear);
		assert_eq(DEFAULT_SEEDMMS,            multiseedMms);
		assert_eq(DEFAULT_SEEDLEN,            multiseedLen);
		assert_eq(DEFAULT_SEEDPERIOD,         multiseedPeriod);
		assert_eq(DEFAULT_IVAL,               multiseedIvalType);
		assert_eq(DEFAULT_IVAL_A,             multiseedIvalA);
		assert_eq(DEFAULT_IVAL_B,             multiseedIvalB);

		assert_eq(DEFAULT_POSMIN,             posmin);
		assert_eq(DEFAULT_POSFRAC,            posfrac);
		assert_eq(DEFAULT_ROWMIN,             rowmin);
		assert_eq(DEFAULT_ROWMULT,            rowmult);
		
		cout << "PASSED" << endl;
	}

	{
		cout << "Case 3: Defaults 3 ... ";
		const char *pol = "";
		SeedAlignmentPolicy::parseString(
			string(pol),
			true,              // --local?
			false,             // noisy homopolymers a la 454?
			bonusMatchType,
			bonusMatch,
			penMmcType,
			penMmc,
			penSnp,
			penNType,
			penN,
			penRdExConst,
			penRfExConst,
			penRdExLinear,
			penRfExLinear,
			costMinConst,
			costMinLinear,
			costFloorConst,
			costFloorLinear,
			nCeilConst,
			nCeilLinear,
			nCatPair,
			multiseedMms,
			multiseedLen,
			multiseedPeriod,
			multiseedIvalType,
			multiseedIvalA,
			multiseedIvalB,
			posmin,
			posfrac,
			rowmin,
			rowmult
		);
		
		assert_eq(DEFAULT_MATCH_BONUS_TYPE_LOCAL,   bonusMatchType);
		assert_eq(DEFAULT_MATCH_BONUS_LOCAL,        bonusMatch);
		assert_eq(DEFAULT_MM_PENALTY_TYPE,    penMmcType);
		assert_eq(DEFAULT_MM_PENALTY,         penMmc);
		assert_eq(DEFAULT_SNP_PENALTY,        penSnp);
		assert_eq(DEFAULT_N_PENALTY_TYPE,     penNType);
		assert_eq(DEFAULT_N_PENALTY,          penN);
		assert_eq(DEFAULT_MIN_CONST_LOCAL,    costMinConst);
		assert_eq(DEFAULT_MIN_LINEAR_LOCAL,   costMinLinear);
		assert_eq(DEFAULT_FLOOR_CONST_LOCAL,  costFloorConst);
		assert_eq(DEFAULT_FLOOR_LINEAR_LOCAL, costFloorLinear);
		assert_eq(DEFAULT_N_CEIL_CONST,       nCeilConst);
		assert_eq(DEFAULT_N_CEIL_LINEAR,      nCeilLinear);
		assert_eq(DEFAULT_N_CAT_PAIR,         nCatPair);

		assert_eq(DEFAULT_READ_GAP_CONST,     penRdExConst);
		assert_eq(DEFAULT_READ_GAP_LINEAR,    penRdExLinear);
		assert_eq(DEFAULT_REF_GAP_CONST,      penRfExConst);
		assert_eq(DEFAULT_REF_GAP_LINEAR,     penRfExLinear);
		assert_eq(DEFAULT_SEEDMMS,            multiseedMms);
		assert_eq(DEFAULT_SEEDLEN,            multiseedLen);
		assert_eq(DEFAULT_SEEDPERIOD,         multiseedPeriod);
		assert_eq(DEFAULT_IVAL,               multiseedIvalType);
		assert_eq(DEFAULT_IVAL_A,             multiseedIvalA);
		assert_eq(DEFAULT_IVAL_B,             multiseedIvalB);

		assert_eq(DEFAULT_POSMIN,             posmin);
		assert_eq(DEFAULT_POSFRAC,            posfrac);
		assert_eq(DEFAULT_ROWMIN,             rowmin);
		assert_eq(DEFAULT_ROWMULT,            rowmult);
		
		cout << "PASSED" << endl;
	}

	{
		cout << "Case 4: Simple string 1 ... ";
		const char *pol = "MMP=C44;MA=4;RFG=24,12;FL=8;RDG=2;SNP=10;NP=C4;MIN=7";
		SeedAlignmentPolicy::parseString(
			string(pol),
			true,              // --local?
			false,             // noisy homopolymers a la 454?
			bonusMatchType,
			bonusMatch,
			penMmcType,
			penMmc,
			penSnp,
			penNType,
			penN,
			penRdExConst,
			penRfExConst,
			penRdExLinear,
			penRfExLinear,
			costMinConst,
			costMinLinear,
			costFloorConst,
			costFloorLinear,
			nCeilConst,
			nCeilLinear,
			nCatPair,
			multiseedMms,
			multiseedLen,
			multiseedPeriod,
			multiseedIvalType,
			multiseedIvalA,
			multiseedIvalB,
			posmin,
			posfrac,
			rowmin,
			rowmult
		);
		
		assert_eq(COST_MODEL_CONSTANT,        bonusMatchType);
		assert_eq(4,                          bonusMatch);
		assert_eq(COST_MODEL_CONSTANT,        penMmcType);
		assert_eq(44,                         penMmc);
		assert_eq(10,                         penSnp);
		assert_eq(COST_MODEL_CONSTANT,        penNType);
		assert_eq(4.0f,                       penN);
		assert_eq(7,                          costMinConst);
		assert_eq(DEFAULT_MIN_LINEAR_LOCAL,   costMinLinear);
		assert_eq(8,                          costFloorConst);
		assert_eq(DEFAULT_FLOOR_LINEAR_LOCAL, costFloorLinear);
		assert_eq(DEFAULT_N_CEIL_CONST,       nCeilConst);
		assert_eq(DEFAULT_N_CEIL_LINEAR,      nCeilLinear);
		assert_eq(DEFAULT_N_CAT_PAIR,         nCatPair);

		assert_eq(2.0f,                       penRdExConst);
		assert_eq(DEFAULT_READ_GAP_LINEAR,    penRdExLinear);
		assert_eq(24.0f,                      penRfExConst);
		assert_eq(12.0f,                      penRfExLinear);
		assert_eq(DEFAULT_SEEDMMS,            multiseedMms);
		assert_eq(DEFAULT_SEEDLEN,            multiseedLen);
		assert_eq(DEFAULT_SEEDPERIOD,         multiseedPeriod);
		assert_eq(DEFAULT_IVAL,               multiseedIvalType);
		assert_eq(DEFAULT_IVAL_A,             multiseedIvalA);
		assert_eq(DEFAULT_IVAL_B,             multiseedIvalB);

		assert_eq(DEFAULT_POSMIN,             posmin);
		assert_eq(DEFAULT_POSFRAC,            posfrac);
		assert_eq(DEFAULT_ROWMIN,             rowmin);
		assert_eq(DEFAULT_ROWMULT,            rowmult);
		
		cout << "PASSED" << endl;
	}
}
#endif /*def ALIGNER_SEED_POLICY_MAIN*/
