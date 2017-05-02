/***************************************************************************
 *   Copyright (C) 2009 by BUI Quang Minh   *
 *   minh.bui@univie.ac.at   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "modelmarkov.h"
#include <stdlib.h>
#include <string.h>
#include "modelliemarkov.h"
#include "modelunrest.h"


/** number of squaring for scaling-squaring technique */
const int TimeSquare = 10;

//----- declaration of some helper functions -----/
int matexp (double Q[], double t, int n, int TimeSquare, double space[]);
int computeStateFreqFromQMatrix (double Q[], double pi[], int n, double space[]);


//const double MIN_FREQ_RATIO = MIN_FREQUENCY;
//const double MAX_FREQ_RATIO = 1.0/MIN_FREQUENCY;

ModelMarkov::ModelMarkov(PhyloTree *tree, bool reversible)
 : ModelSubst(tree->aln->num_states), EigenDecomposition()
{
    phylo_tree = tree;
    rates = NULL;

    // variables for reversible model
    eigenvalues = eigenvectors = inv_eigenvectors = NULL;
    highest_freq_state = num_states-1;
    freq_type = FREQ_UNKNOWN;
    half_matrix = true;
    highest_freq_state = num_states-1;

    // variables for non-reversible model
    fixed_parameters = false;
    model_parameters = NULL;
    rate_matrix = NULL;
    temp_space = NULL;
    eigenvalues_imag = NULL;
    ceval = cevec = cinv_evec = NULL;

    if (reversible) {
        name = "Rev";
        full_name = "General reversible model";
    } else {
        name = "NonRev";
        full_name = "General non-reversible model";
    }
    setReversible(reversible);
}

void ModelMarkov::setReversible(bool reversible) {
    is_reversible = reversible;

    if (reversible) {
        // setup reversible model
        int i;
        int nrate = getNumRateEntries();

        if (rates)
            delete [] rates;
        rates = new double[nrate];
        for (i=0; i < nrate; i++)
            rates[i] = 1.0;

        if (!eigenvalues)
            eigenvalues = aligned_alloc<double>(num_states);
        if (!eigenvectors)
            eigenvectors = aligned_alloc<double>(num_states*num_states);
        if (!inv_eigenvectors)
            inv_eigenvectors = aligned_alloc<double>(num_states*num_states);

        num_params = nrate - 1;

    } else {
        // setup non-reversible model
        ignore_state_freq = true;

        int num_rates = getNumRateEntries();
        
        // reallocate the mem spaces
        if (rates)
            delete [] rates;
        rates = new double [num_rates];
        memset(rates, 0, sizeof(double) * (num_rates));

        if (!rate_matrix)
            rate_matrix = aligned_alloc<double>(num_states*num_states);
        if (!temp_space)
            temp_space =  aligned_alloc<double>(num_states*num_states);
        if (!eigenvalues_imag)
            eigenvalues_imag = aligned_alloc<double>(num_states);

        if (!ceval)
            ceval = aligned_alloc<complex<double> >(num_states);
        if (!cevec)
            cevec = aligned_alloc<complex<double> >(num_states*num_states);
        if (!cinv_evec)
            cinv_evec = aligned_alloc<complex<double> >(num_states*num_states);
        
        if (!phylo_tree->rooted) {
            cout << "Converting unrooted to rooted tree..." << endl;
            phylo_tree->convertToRooted();
        }
    }
}

int ModelMarkov::getNumRateEntries() {
    if (is_reversible)
        return num_states*(num_states-1) / 2;
    else
        return num_states*(num_states-1);
}

void ModelMarkov::saveCheckpoint() {
    if (!is_reversible) {
        checkpoint->startStruct("ModelMarkov");
        CKP_ARRAY_SAVE(num_params, model_parameters);
        checkpoint->endStruct();
    }
    ModelSubst::saveCheckpoint();
}

void ModelMarkov::restoreCheckpoint() {
    ModelSubst::restoreCheckpoint();
    if (!is_reversible) {
        checkpoint->startStruct("ModelMarkov");
        CKP_ARRAY_RESTORE(num_params, model_parameters);
        checkpoint->endStruct();
        setRates();
    }
}


void ModelMarkov::setTree(PhyloTree *tree) {
	phylo_tree = tree;
}

/*
 * For freq_type, return a "+F" string specifying that freq_type.
 * Note not all freq_types accomodated.
 * Inverse of this occurs in ModelFactory::ModelFactory, 
 * where +F... suffixes on model names get parsed.
 */
string freqTypeString(StateFreqType freq_type, SeqType seq_type, bool full_str) {
    switch(freq_type) {
    case FREQ_UNKNOWN:    return("");
    case FREQ_USER_DEFINED:
        if (seq_type == SEQ_PROTEIN)
            return "";
        else
            return "+FU";
    case FREQ_EQUAL:
        if (seq_type == SEQ_DNA && !full_str)
            return "";
        else
            return "+FQ";
    case FREQ_EMPIRICAL:  return "+F";
    case FREQ_ESTIMATE:
        if (seq_type == SEQ_DNA && !full_str)
            return "";
        else
            return "+FO";
    case FREQ_CODON_1x4:  return("+F1X4");
    case FREQ_CODON_3x4:  return("+F3X4");
    case FREQ_CODON_3x4C: return("+F3X4C");
    case FREQ_MIXTURE:  return(""); // no idea what to do here - MDW
    case FREQ_DNA_RY:   return("+FRY");
    case FREQ_DNA_WS:   return("+FWS");
    case FREQ_DNA_MK:   return("+FMK");
    case FREQ_DNA_1112: return("+F1112");
    case FREQ_DNA_1121: return("+F1121");
    case FREQ_DNA_1211: return("+F1211");
    case FREQ_DNA_2111: return("+F2111");
    case FREQ_DNA_1122: return("+F1122");
    case FREQ_DNA_1212: return("+F1212");
    case FREQ_DNA_1221: return("+F1221");
    case FREQ_DNA_1123: return("+F1123");
    case FREQ_DNA_1213: return("+F1213");
    case FREQ_DNA_1231: return("+F1231");
    case FREQ_DNA_2113: return("+F2113");
    case FREQ_DNA_2131: return("+F2131");
    case FREQ_DNA_2311: return("+F2311");
    default: throw("Unrecoginzed freq_type in freqTypeString - can't happen");
    }
}

string ModelMarkov::getName() {
  // MDW note to Minh for code review: I don't really understand what getName()
  // is used for. I've tried to keep the old behaviour while adding
  // the new freq_types, but give this change extra attention please.
    return name+freqTypeString(getFreqType(), phylo_tree->aln->seq_type, false);
  /*
	if (getFreqType() == FREQ_EMPIRICAL)
		return name + "+F";
	else if (getFreqType() == FREQ_CODON_1x4)
		return name += "+F1X4";
	else if (getFreqType() == FREQ_CODON_3x4)
		return name + "+F3X4";
	else if (getFreqType() == FREQ_CODON_3x4C)
		return name + "+F3X4C";
	else if (getFreqType() == FREQ_ESTIMATE && phylo_tree->aln->seq_type != SEQ_DNA)
		return name + "+FO";
	else if (getFreqType() == FREQ_EQUAL && phylo_tree->aln->seq_type != SEQ_DNA)
		return name + "+FQ";
    else
        return name;
  */
}

string ModelMarkov::getNameParams() {

	ostringstream retname;
	retname << name;
//	if (num_states != 4) retname << num_states;
	retname << '{';
	int nrates = getNumRateEntries();
	for (int i = 0; i < nrates; i++) {
		if (i>0) retname << ',';
		retname << rates[i];
	}
	retname << '}';
    getNameParamsFreq(retname);
    return retname.str();    
}
    
void ModelMarkov::getNameParamsFreq(ostream &retname) {
     // "+F..." but without {frequencies}
    retname << freqTypeString(freq_type, phylo_tree->aln->seq_type, true);

    if (freq_type == FREQ_EMPIRICAL || freq_type == FREQ_ESTIMATE ||
        (freq_type == FREQ_USER_DEFINED && phylo_tree->aln->seq_type == SEQ_DNA)) {
        retname << "{" << state_freq[0];
        for (int i = 1; i < num_states; i++)
            retname << "," << state_freq[i];
        retname << "}";
    }
}

void ModelMarkov::init_state_freq(StateFreqType type) {
    //if (type == FREQ_UNKNOWN) return;
    int i;
    freq_type = type;
    assert(freq_type != FREQ_UNKNOWN);
    switch (freq_type) {
    case FREQ_EQUAL:
        if (phylo_tree->aln->seq_type == SEQ_CODON) {
             int nscodon = phylo_tree->aln->getNumNonstopCodons();
             double freq_codon = (1.0-(num_states-nscodon)*MIN_FREQUENCY)/(nscodon);
             for (i = 0; i < num_states; i++)
                 if (phylo_tree->aln->isStopCodon(i))
                     state_freq[i] = MIN_FREQUENCY;
                 else
                     state_freq[i] = freq_codon;
        } else {
            double freq_state = 1.0/num_states;
            for (i = 0; i < num_states; i++)
                state_freq[i] = freq_state;
        }
        break;  
    case FREQ_ESTIMATE:
    case FREQ_EMPIRICAL:
        if (phylo_tree->aln->seq_type == SEQ_CODON) {
            double ntfreq[12];
            phylo_tree->aln->computeCodonFreq(freq_type, state_freq, ntfreq);
//                      phylo_tree->aln->computeCodonFreq(state_freq);
        } else if (phylo_tree->aln->seq_type != SEQ_POMO)
            phylo_tree->aln->computeStateFreq(state_freq);
        for (i = 0; i < num_states; i++)
            if (state_freq[i] > state_freq[highest_freq_state])
                highest_freq_state = i;
        break;
    case FREQ_USER_DEFINED:
        if (state_freq[0] == 0.0) outError("State frequencies not specified");
        break;
    default: break;
    }
    if (phylo_tree->aln->seq_type == SEQ_DNA) {
        // BQM 2017-05-02: first, empirically count state_freq from alignment
        if (freq_type >= FREQ_DNA_RY)
            phylo_tree->aln->computeStateFreq(state_freq);

        // For complex DNA freq_types, adjust state_freq to conform to that freq_type.
        forceFreqsConform(state_freq, freq_type);
    }
}

void ModelMarkov::init(StateFreqType type) {
        init_state_freq(type);
	decomposeRateMatrix();
	if (verbose_mode >= VB_MAX)
		writeInfo(cout);
}

void ModelMarkov::writeInfo(ostream &out) {
	if (is_reversible && num_states == 4) {
		out << "Rate parameters:";
		//out.precision(3);
		//out << fixed;
		out << "  A-C: " << rates[0];
		out << "  A-G: " << rates[1];
		out << "  A-T: " << rates[2];
		out << "  C-G: " << rates[3];
		out << "  C-T: " << rates[4];
		out << "  G-T: " << rates[5];
		out << endl;
		//if (freq_type != FREQ_ESTIMATE) return;
		out << "Base frequencies: ";
		out << "  A: " << state_freq[0];
		out << "  C: " << state_freq[1];
		out << "  G: " << state_freq[2];
		out << "  T: " << state_freq[3];
		out << endl;
	} else if (!is_reversible) {
        // non-reversible
        int i;
        out << "Model parameters: ";
        if (num_params>0) out << model_parameters[0];
        for (i=1; i < num_params; i++) out << "," << model_parameters[i];
        out << endl;

        if (num_states != 4) return;
		out << "Substitution rates:" << endl;
		out << "  A-C: " << rates[0];
		out << "  A-G: " << rates[1];
		out << "  A-T: " << rates[2];
        out << "  C-A: " << rates[3];
		out << "  C-G: " << rates[4];
		out << "  C-T: " << rates[5] << endl;
        out << "  G-A: " << rates[6];
        out << "  G-C: " << rates[7];
		out << "  G-T: " << rates[8];
        out << "  T-A: " << rates[9];
        out << "  T-C: " << rates[10];
        out << "  T-G: " << rates[11];
		out << endl;
		out << "Base frequencies: ";
		out << "  A: " << state_freq[0];
		out << "  C: " << state_freq[1];
		out << "  G: " << state_freq[2];
		out << "  T: " << state_freq[3];
		out << endl;
    }
}

void ModelMarkov::computeTransMatrix(double time, double *trans_matrix, int mixture) {

    if (!is_reversible) {
        if (phylo_tree->params->matrix_exp_technique == MET_EIGEN_DECOMPOSITION) {
            computeTransMatrixEigen(time, trans_matrix);
        } else {
            // scaling and squaring technique
            int statesqr = num_states*num_states;
            memcpy(trans_matrix, rate_matrix, statesqr*sizeof(double));
            matexp(trans_matrix, time, num_states, TimeSquare, temp_space);
        }
        return;
        // 2016-04-05: 2nd version
    //    for (int i = 0; i < statesqr; i++) 
    //        trans_matrix[i] *= time;
    //    double space[NCODE*NCODE*3] = {0};
    //    matexp2(trans_matrix, num_states, 7, 5, space);
    }

	/* compute P(t) */
	double evol_time = time / total_num_subst;
	double exptime[num_states];
	int i, j, k;

	for (i = 0; i < num_states; i++)
		exptime[i] = exp(evol_time * eigenvalues[i]);

	int row_offset;
	for (i = 0, row_offset = 0; i < num_states; i++, row_offset+=num_states) {
		double *trans_row = trans_matrix + row_offset;
		for (j = i+1; j < num_states; j ++) { 
			// compute upper triangle entries
			double *trans_entry = trans_row + j;
//			double *coeff_entry = eigen_coeff + ((row_offset+j)*num_states);
			*trans_entry = 0.0;
			for (k = 0; k < num_states; k ++) {
				*trans_entry += eigenvectors[i*num_states+k] * inv_eigenvectors[k*num_states+j] * exptime[k];
			}
			if (*trans_entry < 0.0) {
				*trans_entry = 0.0;
			}
			// update lower triangle entries
			trans_matrix[j*num_states+i] = (state_freq[i]/state_freq[j]) * (*trans_entry);
		}
		trans_row[i] = 0.0; // initialize diagonal entry
		// taking the sum of row
		double sum = 0.0;
		for (j = 0; j < num_states; j++)
			sum += trans_row[j];
		trans_row[i] = 1.0 - sum; // update diagonal entry
	}
//	delete [] exptime;
}

double ModelMarkov::computeTrans(double time, int state1, int state2) {

    if (is_reversible) {
        double evol_time = time / total_num_subst;
        int i;
        double trans_prob = 0.0;
        for (i = 0; i < num_states; i++) {
            trans_prob += eigenvectors[state1*num_states+i] * inv_eigenvectors[i*num_states+state2] * exp(evol_time * eigenvalues[i]);
        }
        return trans_prob;
    } else {
        // non-reversible
//        double *trans_matrix = new double[num_states*num_states];
        computeTransMatrix(time, temp_space);
        double trans = temp_space[state1*num_states+state2];
//        delete [] trans_matrix;
        return trans;
    }
}

double ModelMarkov::computeTrans(double time, int state1, int state2, double &derv1, double &derv2) {
	double evol_time = time / total_num_subst;
	int i;

//	double *coeff_entry = eigen_coeff + ((state1*num_states+state2)*num_states);
	double trans_prob = 0.0;
	derv1 = derv2 = 0.0;
	for (i = 0; i < num_states; i++) {
		double trans = eigenvectors[state1*num_states+i] * inv_eigenvectors[i*num_states+state2] * exp(evol_time * eigenvalues[i]);
		double trans2 = trans * eigenvalues[i];
		trans_prob += trans;
		derv1 += trans2;
		derv2 += trans2 * eigenvalues[i];
	}
	return trans_prob;
}


void ModelMarkov::computeTransDerv(double time, double *trans_matrix, 
	double *trans_derv1, double *trans_derv2, int mixture)
{
	int i, j, k;

    if (!is_reversible) {
        computeTransMatrix(time, trans_matrix);
        // First derivative = Q * e^(Qt)
        for (i = 0; i < num_states; i++)
            for (j = 0; j < num_states; j++) {
                double val = 0.0;
                for (k = 0; k < num_states; k++)
                    val += rate_matrix[i*num_states+k] * trans_matrix[k*num_states+j];
                trans_derv1[i*num_states+j] = val;
            }
            
        // Second derivative = Q * Q * e^(Qt)
        for (i = 0; i < num_states; i++)
            for (j = 0; j < num_states; j++) {
                double val = 0.0;
                for (k = 0; k < num_states; k++)
                    val += rate_matrix[i*num_states+k] * trans_derv1[k*num_states+j];
                trans_derv2[i*num_states+j] = val;
            }
        return;
    }

	double evol_time = time / total_num_subst;
	double exptime[num_states];

	for (i = 0; i < num_states; i++)
		exptime[i] = exp(evol_time * eigenvalues[i]);

	for (i = 0; i < num_states; i ++) {
		for (j = 0; j < num_states; j ++) {
			int offset = (i*num_states+j);
			double *trans_entry = trans_matrix + offset;
			double *derv1_entry = trans_derv1 + offset;
			double *derv2_entry = trans_derv2 + offset;

//			int coeff_offset = offset*num_states;
//			double *coeff_entry       = eigen_coeff + coeff_offset;
			*trans_entry = 0.0;
			*derv1_entry = 0.0;
			*derv2_entry = 0.0;
			for (k = 0; k < num_states; k ++) {
				double trans = eigenvectors[i*num_states+k] * inv_eigenvectors[k*num_states+j] * exptime[k];
				double trans2 = trans * eigenvalues[k];
				*trans_entry += trans;
				*derv1_entry += trans2;
				*derv2_entry += trans2 * eigenvalues[k];
			}
			if (*trans_entry < 0.0) {
				*trans_entry = 0.0;
			}
		}
	}
//	delete [] exptime;
}

void ModelMarkov::getRateMatrix(double *rate_mat) {
	int nrate = getNumRateEntries();
	memcpy(rate_mat, rates, nrate * sizeof(double));
}

void ModelMarkov::setRateMatrix(double* rate_mat)
{
	int nrate = getNumRateEntries();
	memcpy(rates, rate_mat, nrate * sizeof(double));
}

void ModelMarkov::getStateFrequency(double *freq, int mixture) {
	assert(state_freq);
	assert(freq_type != FREQ_UNKNOWN);
	memcpy(freq, state_freq, sizeof(double) * num_states);
    // 2015-09-07: relax the sum of state_freq to be 1, this will be done at the end of optimization
    double sum = 0.0;
    int i;
    for (i = 0; i < num_states; i++) sum += freq[i];
    sum = 1.0/sum;
    for (i = 0; i < num_states; i++) freq[i] *= sum;
}

void ModelMarkov::setStateFrequency(double* freq)
{
	assert(state_freq);
	memcpy(state_freq, freq, sizeof(double) * num_states);
}

void ModelMarkov::getQMatrix(double *q_mat) {

    if (!is_reversible) {
        // non-reversible model
        memmove(q_mat, rate_matrix, num_states*num_states*sizeof(double));
        return;
    }

	double **rate_matrix = (double**) new double[num_states];
	int i, j, k = 0;

	for (i = 0; i < num_states; i++)
		rate_matrix[i] = new double[num_states];

	for (i = 0, k = 0; i < num_states; i++) {
		rate_matrix[i][i] = 0.0;
		for (j = i+1; j < num_states; j++, k++) {
			rate_matrix[i][j] = rates[k];
			rate_matrix[j][i] = rates[k];
		}
	}

	computeRateMatrix(rate_matrix, state_freq, num_states);
	for (i = 0; i < num_states; i++)
		memmove(q_mat + (i*num_states), rate_matrix[i], num_states * sizeof(double));

	for (i = num_states-1; i >= 0; i--)
		delete [] rate_matrix[i];
	delete [] rate_matrix;

}

int ModelMarkov::getNDim() { 
	assert(freq_type != FREQ_UNKNOWN);
	if (fixed_parameters)
		return 0;
    if (!is_reversible)
        return (num_params);

    // reversible model
	int ndim = num_params;
	if (freq_type == FREQ_ESTIMATE) 
		ndim += num_states-1;
	return ndim;
}

int ModelMarkov::getNDimFreq() { 

    // BQM, 2017-05-02: getNDimFreq should return degree of freedom, which is not included in getNDim()
    // That's why 0 is returned for FREQ_ESTIMATE, num_states-1 for FREQ_EMPIRICAL

	if (freq_type == FREQ_EMPIRICAL)
        return num_states-1;
	else if (freq_type == FREQ_CODON_1x4) 
        return 3;
	else if (freq_type == FREQ_CODON_3x4 || freq_type == FREQ_CODON_3x4C) 
        return 9;

    // commented out due to reason above
//	if (phylo_tree->aln->seq_type == SEQ_DNA) {
//            return nFreqParams(freq_type);
//	}
	return 0;
}

void ModelMarkov::scaleStateFreq(bool sum_one) {
	int i;
	if (sum_one) {
		// make the frequencies sum to 1
		double sum = 0.0;
		for (i = 0; i < num_states; i++) sum += state_freq[i];
		for (i = 0; i < num_states; i++) state_freq[i] /= sum;		
	} else {
		// make the last frequency equal to 0.1
		if (state_freq[num_states-1] == 0.1) return;
		assert(state_freq[num_states-1] > 1.1e-6);
		for (i = 0; i < num_states; i++) 
			state_freq[i] /= state_freq[num_states-1]*10.0;
	}
}

void ModelMarkov::setVariables(double *variables) {
	int nrate = getNDim();

    // non-reversible case
    if (!is_reversible) {
        if (nrate > 0)
            memcpy(variables+1, model_parameters, nrate*sizeof(double));
        return;
    }

	if (freq_type == FREQ_ESTIMATE) nrate -= (num_states-1);
	if (nrate > 0)
		memcpy(variables+1, rates, nrate*sizeof(double));
	if (freq_type == FREQ_ESTIMATE) {
        // 2015-09-07: relax the sum of state_freq to be 1, this will be done at the end of optimization
		int ndim = getNDim();
		memcpy(variables+(ndim-num_states+2), state_freq, (num_states-1)*sizeof(double));
    }
}

bool ModelMarkov::getVariables(double *variables) {
	int nrate = getNDim();
	int i;
	bool changed = false;

    // non-reversible case
    if (!is_reversible) {
        for (i = 0; i < nrate && !changed; i++)
            changed = (model_parameters[i] != variables[i+1]);
        if (changed) {
            memcpy(model_parameters, variables+1, nrate * sizeof(double));
            setRates();
        }
        return changed;
    }

	if (freq_type == FREQ_ESTIMATE) nrate -= (num_states-1);
	if (nrate > 0) {
		for (i = 0; i < nrate; i++)
			changed |= (rates[i] != variables[i+1]);
		memcpy(rates, variables+1, nrate * sizeof(double));
	}

	if (freq_type == FREQ_ESTIMATE) {
        // 2015-09-07: relax the sum of state_freq to be 1, this will be done at the end of optimization
        // 2015-09-07: relax the sum of state_freq to be 1, this will be done at the end of optimization
		int ndim = getNDim();
		for (i = 0; i < num_states-1; i++)
			changed |= (state_freq[i] != variables[i+ndim-num_states+2]);
		memcpy(state_freq, variables+(ndim-num_states+2), (num_states-1)*sizeof(double));

//		memcpy(state_freq, variables+nrate+1, (num_states-1)*sizeof(double));
		//state_freq[num_states-1] = 0.1;
		//scaleStateFreq(true);

//		double sum = 0.0;
//		for (int i = 0; i < num_states-1; i++)
//			sum += state_freq[i];
//		state_freq[num_states-1] = 1.0 - sum;
//		double sum = 1.0;
//		int i, j;
//		for (i = 1; i < num_states; i++)
//			sum += variables[nrate+i];
//		for (i = 0, j = 1; i < num_states; i++)
//			if (i != highest_freq_state) {
//				state_freq[i] = variables[nrate+j] / sum;
//				j++;
//			}
//		state_freq[highest_freq_state] = 1.0/sum;
	}
	return changed;
}

double ModelMarkov::targetFunk(double x[]) {
	bool changed = getVariables(x);

    if (is_reversible && state_freq[num_states-1] < 1e-4) return 1.0e+12;

	if (changed) {
		decomposeRateMatrix();
		assert(phylo_tree);
		phylo_tree->clearAllPartialLH();
	}

    // avoid numerical issue if state_freq is too small
    for (int i = 0; i < num_states; i++)
        if (state_freq[i] < 1e-4)
            return 1.0e+12;

	return -phylo_tree->computeLikelihood();

}

bool ModelMarkov::isUnstableParameters() {
	int nrates = getNumRateEntries();
	int i;
    // NOTE: zero rates are not consider unstable anymore
	for (i = 0; i < nrates; i++)
		if (/*rates[i] < MIN_RATE+TOL_RATE || */rates[i] > MAX_RATE-TOL_RATE)
			return true;
	for (i = 0; i < num_states; i++)
		if (state_freq[i] < MIN_RATE+TOL_RATE)
			return true;
	return false;
}

void ModelMarkov::setBounds(double *lower_bound, double *upper_bound, bool *bound_check) {
    assert(is_reversible && "setBounds should only be called on subclass of ModelMarkov");

    int i, ndim = getNDim();

    for (i = 1; i <= ndim; i++) {
	//cout << variables[i] << endl;
	lower_bound[i] = MIN_RATE;
	upper_bound[i] = MAX_RATE;
	bound_check[i] = false;
    }

	if (freq_type == FREQ_ESTIMATE) {
		for (i = ndim-num_states+2; i <= ndim; i++) {
//            lower_bound[i] = MIN_FREQUENCY/state_freq[highest_freq_state];
//			upper_bound[i] = state_freq[highest_freq_state]/MIN_FREQUENCY;
            lower_bound[i]  = MIN_FREQUENCY;
//            upper_bound[i] = 100.0;
            upper_bound[i] = 1.0;
            bound_check[i] = false;
        }
	} else if (phylo_tree->aln->seq_type == SEQ_DNA) {
        setBoundsForFreqType(&lower_bound[num_params+1], &upper_bound[num_params+1],
            &bound_check[num_params+1], MIN_FREQUENCY, freq_type);
    }
}

double ModelMarkov::optimizeParameters(double gradient_epsilon) {
	int ndim = getNDim();
	
	// return if nothing to be optimized
	if (ndim == 0) return 0.0;
    
	if (verbose_mode >= VB_MAX)
		cout << "Optimizing " << name << " model parameters..." << endl;

	//if (freq_type == FREQ_ESTIMATE) scaleStateFreq(false);

	double *variables = new double[ndim+1];
	double *upper_bound = new double[ndim+1];
	double *lower_bound = new double[ndim+1];
	bool *bound_check = new bool[ndim+1];
	double score;

    for (int i = 0; i < num_states; i++)
        if (state_freq[i] > state_freq[highest_freq_state])
            highest_freq_state = i;

	// by BFGS algorithm
	setVariables(variables);
	setBounds(lower_bound, upper_bound, bound_check);
    if (phylo_tree->params->optimize_alg.find("BFGS-B") == string::npos)
        score = -minimizeMultiDimen(variables, ndim, lower_bound, upper_bound, bound_check, max(gradient_epsilon, TOL_RATE));
    else
        score = -L_BFGS_B(ndim, variables+1, lower_bound+1, upper_bound+1, max(gradient_epsilon, TOL_RATE));

	bool changed = getVariables(variables);
    // BQM 2015-09-07: normalize state_freq
	if (is_reversible && freq_type == FREQ_ESTIMATE) {
        scaleStateFreq(true);
        changed = true;
    }
    if (changed) {
        decomposeRateMatrix();
        phylo_tree->clearAllPartialLH();
        score = phylo_tree->computeLikelihood();
    }
	
	delete [] bound_check;
	delete [] lower_bound;
	delete [] upper_bound;
	delete [] variables;

	return score;
}

void ModelMarkov::decomposeRateMatrix(){
	int i, j, k = 0;

    if (!is_reversible) {
        double sum;
        //double m[num_states];
        double *space = new double[num_states*(num_states+1)];

        for (i = 0; i < num_states; i++)
            state_freq[i] = 1.0/num_states;

        for (i = 0, k = 0; i < num_states; i++) {
            rate_matrix[i*num_states+i] = 0.0;
            double row_sum = 0.0;
            for (j = 0; j < num_states; j++)
                if (j != i) {
                    row_sum += (rate_matrix[i*num_states+j] = rates[k++]);
                }
            rate_matrix[i*num_states+i] = -row_sum;
        }
        computeStateFreqFromQMatrix(rate_matrix, state_freq, num_states, space);


        for (i = 0, sum = 0.0; i < num_states; i++) {
            sum -= rate_matrix[i*num_states+i] * state_freq[i]; /* exp. rate */
        }

        if (sum == 0.0) throw "Empty Q matrix";

        double delta = total_num_subst / sum; /* 0.01 subst. per unit time */

        for (i = 0; i < num_states; i++) {
            for (j = 0; j < num_states; j++) {
                rate_matrix[i*num_states+j] *= delta;
            }
        }
        delete [] space;

        if (phylo_tree->params->matrix_exp_technique == MET_EIGEN_DECOMPOSITION) {
            eigensystem_nonrev(rate_matrix, state_freq, eigenvalues, eigenvalues_imag, eigenvectors, inv_eigenvectors, num_states);
        }
    } else if (num_params == -1) {
        // reversible model
		// manual compute eigenvalues/vectors for F81-style model
		eigenvalues[0] = 0.0;
		double mu = 0.0;
		for (i = 0; i < num_states; i++)
			mu += state_freq[i]*state_freq[i];
		mu = total_num_subst/(1.0 - mu);

		// compute eigenvalues
		for (i = 1; i < num_states; i++)
			eigenvalues[i] = -mu;

//		double *f = new double[num_states];
//		for (i = 0; i < num_states; i++) f[i] = sqrt(state_freq[i]);
		// compute eigenvectors
		memset(eigenvectors, 0, num_states*num_states*sizeof(double));
		memset(inv_eigenvectors, 0, num_states*num_states*sizeof(double));
		eigenvectors[0] = 1.0;
		for (i = 1; i < num_states; i++)
			eigenvectors[i] = -1.0;
//			eigenvectors[i] = f[i]/f[num_states-1];
		for (i = 1; i < num_states; i++) {
			eigenvectors[i*num_states] = 1.0;
			eigenvectors[i*num_states+i] = state_freq[0]/state_freq[i];
		}

		for (i = 0; i < num_states; i++)
			for (j = 0; j < num_states; j++)
				inv_eigenvectors[i*num_states+j] = state_freq[j]*eigenvectors[j*num_states+i];
		writeInfo(cout);
		// sanity check
		double *q = new double[num_states*num_states];
		getQMatrix(q);
		double zero;
		for (j = 0; j < num_states; j++) {
			for (i = 0, zero = 0.0; i < num_states; i++) {
				for (k = 0; k < num_states; k++) zero += q[i*num_states+k] * eigenvectors[k*num_states+j];
				zero -= eigenvalues[j] * eigenvectors[i*num_states+j];
				if (fabs(zero) > 1.0e-5) {
					cout << "\nERROR: Eigenvector doesn't satisfy eigenvalue equation! (gap=" << fabs(zero) << ")" << endl;
					abort();
				}
			}
		}
		delete [] q;
	} else {

        // general reversible model
		double **rate_matrix = new double*[num_states];

		for (i = 0; i < num_states; i++)
			rate_matrix[i] = new double[num_states];

        if (half_matrix) {
            for (i = 0, k = 0; i < num_states; i++) {
                rate_matrix[i][i] = 0.0;
                for (j = i+1; j < num_states; j++, k++) {
                    rate_matrix[i][j] = rates[k];
                    rate_matrix[j][i] = rates[k];
                }
            }
        } else {
            // full matrix
            for (i = 0; i < num_states; i++) {
                memcpy(rate_matrix[i], &rates[i*num_states], num_states*sizeof(double));
                rate_matrix[i][i] = 0.0;
            }
        }
		/* eigensystem of 1 PAM rate matrix */
		eigensystem_sym(rate_matrix, state_freq, eigenvalues, eigenvectors, inv_eigenvectors, num_states);
		//eigensystem(rate_matrix, state_freq, eigenvalues, eigenvectors, inv_eigenvectors, num_states);
		for (i = num_states-1; i >= 0; i--)
			delete [] rate_matrix[i];
		delete [] rate_matrix;
	}
}

void ModelMarkov::readRates(istream &in) throw(const char*, string) {
	int nrates = getNumRateEntries();
	string str;
	in >> str;
	if (str == "equalrate") {
		for (int i = 0; i < nrates; i++)
			rates[i] = 1.0;
	} else {
		try {
			rates[0] = convert_double(str.c_str());
		} catch (string &str) {
			outError(str);
		}
		if (rates[0] < 0.0)
			throw "Negative rates not allowed";
		for (int i = 1; i < nrates; i++) {
			if (!(in >> rates[i]))
				throw "Rate entries could not be read";
			if (rates[i] < 0.0)
				throw "Negative rates not allowed";
		}
	}
}

void ModelMarkov::readRates(string str) throw(const char*) {
	int nrates = getNumRateEntries();
	int end_pos = 0;
	cout << __func__ << " " << str << endl;
	if (str.find("equalrate") != string::npos) {
		for (int i = 0; i < nrates; i++)
			rates[i] = 1.0;
	} else for (int i = 0; i < nrates; i++) {
		int new_end_pos;
		try {
			rates[i] = convert_double(str.substr(end_pos).c_str(), new_end_pos);
		} catch (string &str) {
			outError(str);
		}
		end_pos += new_end_pos;
		if (rates[i] <= 0.0)
			outError("Non-positive rates found");
		if (i == nrates-1 && end_pos < str.length())
			outError("String too long ", str);
		if (i < nrates-1 && end_pos >= str.length())
			outError("Unexpected end of string ", str);
		if (end_pos < str.length() && str[end_pos] != ',')
			outError("Comma to separate rates not found in ", str);
		end_pos++;
	}
	num_params = 0;

}

void ModelMarkov::readStateFreq(istream &in) throw(const char*) {
	int i;
	for (i = 0; i < num_states; i++) {
		if (!(in >> state_freq[i])) 
			throw "State frequencies could not be read";
		if (state_freq[i] < 0.0)
			throw "Negative state frequencies found";
	}
	double sum = 0.0;
	for (i = 0; i < num_states; i++) sum += state_freq[i];
	if (fabs(sum-1.0) > 1e-2)
		throw "State frequencies do not sum up to 1.0";
}

void ModelMarkov::readStateFreq(string str) throw(const char*) {
	int i;
	int end_pos = 0;
	for (i = 0; i < num_states; i++) {
		int new_end_pos;
		state_freq[i] = convert_double(str.substr(end_pos).c_str(), new_end_pos);
		end_pos += new_end_pos;
		//cout << i << " " << state_freq[i] << endl;
		if (state_freq[i] < 0.0 || state_freq[i] > 1)
			outError("State frequency must be in [0,1] in ", str);
		if (i == num_states-1 && end_pos < str.length())
			outError("Unexpected end of string ", str);
		if (end_pos < str.length() && str[end_pos] != ',' && str[end_pos] != ' ')
			outError("Comma/Space to separate state frequencies not found in ", str);
		end_pos++;
	}
	double sum = 0.0;
	for (i = 0; i < num_states; i++) sum += state_freq[i];
	if (fabs(sum-1.0) > 1e-2)
		outError("State frequencies do not sum up to 1.0 in ", str);
}

void ModelMarkov::readParameters(const char *file_name) { 
	try {
		ifstream in(file_name);
		if (in.fail()) {
			outError("Invalid model name ", file_name);
        }
		cout << "Reading model parameters from file " << file_name << endl;
		readRates(in);
		readStateFreq(in);
		in.close();
	}
	catch (const char *str) {
		outError(str);
	} 
	num_params = 0;
	writeInfo(cout);
}


ModelMarkov::~ModelMarkov() {
	freeMem();
}

void ModelMarkov::freeMem()
{
    if (inv_eigenvectors)
        aligned_free(inv_eigenvectors);
    if (eigenvectors)
        aligned_free(eigenvectors);
    if (eigenvalues)
        aligned_free(eigenvalues);

	if (rates) delete [] rates;

    if (cinv_evec)
        aligned_free(cinv_evec);
    if (cevec)
        aligned_free(cevec);
    if (ceval)
        aligned_free(ceval);
    if (eigenvalues_imag)
        aligned_free(eigenvalues_imag);
    if (temp_space)
        aligned_free(temp_space);
    if (rate_matrix)
        aligned_free(rate_matrix);
    if (model_parameters)
        delete [] model_parameters;
}

double *ModelMarkov::getEigenvalues() const
{
    return eigenvalues;
}

double *ModelMarkov::getEigenvectors() const
{
    return eigenvectors;
}

double* ModelMarkov::getInverseEigenvectors() const {
	return inv_eigenvectors;
}

//void ModelGTR::setEigenCoeff(double *eigenCoeff)
//{
//    eigen_coeff = eigenCoeff;
//}

void ModelMarkov::setEigenvalues(double *eigenvalues)
{
    this->eigenvalues = eigenvalues;
}

void ModelMarkov::setEigenvectors(double *eigenvectors)
{
    this->eigenvectors = eigenvectors;
}

void ModelMarkov::setInverseEigenvectors(double *inv_eigenvectors)
{
    this->inv_eigenvectors = inv_eigenvectors;
}

/****************************************************/
/*      NON-REVERSIBLE STUFFS                       */
/****************************************************/


void ModelMarkov::setRates() {
	// I don't know the proper C++ way to handle this: got error if I didn't define something here.
	assert(0 && "setRates should only be called on subclass of ModelMarkov");
}

/* static */ ModelMarkov* ModelMarkov::getModelByName(string model_name, PhyloTree *tree, string model_params, StateFreqType freq_type, string freq_params) {
	if (ModelUnrest::validModelName(model_name)) {
		return (new ModelUnrest(tree, model_params));
	} else if (ModelLieMarkov::validModelName(model_name)) {
	        return (new ModelLieMarkov(model_name, tree, model_params, freq_type, freq_params));
	} else {
		cerr << "Unrecognized model name " << model_name << endl;
		return (NULL);
	}
}

/* static */ bool ModelMarkov::validModelName(string model_name) {
	return ModelUnrest::validModelName(model_name) 
	  || ModelLieMarkov::validModelName(model_name);
}



void ModelMarkov::computeTransMatrixEigen(double time, double *trans_matrix) {
	/* compute P(t) */
	double evol_time = time / total_num_subst;
    int nstates_2 = num_states*num_states;
	double *exptime = new double[nstates_2];
	int i, j, k;

    memset(exptime, 0, sizeof(double)*nstates_2);
	for (i = 0; i < num_states; i++)
        if (eigenvalues_imag[i] == 0.0) {
            exptime[i*num_states+i] = exp(evol_time * eigenvalues[i]);
        } else {
            assert(i < num_states-1 && eigenvalues_imag[i+1] != 0.0 && eigenvalues_imag[i] > 0.0);
            complex<double> exp_eval(eigenvalues[i] * evol_time, eigenvalues_imag[i] * evol_time);
            exp_eval = exp(exp_eval);
            exptime[i*num_states+i] = exp_eval.real();
            exptime[i*num_states+i+1] = exp_eval.imag();
            i++;
            exptime[i*num_states+i] = exp_eval.real();
            exptime[i*num_states+i-1] = -exp_eval.imag();
        }


    // compute V * exp(L t)
    for (i = 0; i < num_states; i++)
        for (j = 0; j < num_states; j++) {
            double val = 0;
            for (k = 0; k < num_states; k++)
                val += eigenvectors[i*num_states+k] * exptime[k*num_states+j];
            trans_matrix[i*num_states+j] = val;
        }

    memcpy(exptime, trans_matrix, sizeof(double)*nstates_2);

    // then compute V * exp(L t) * V^{-1}
    for (i = 0; i < num_states; i++) {
        double row_sum = 0.0;
        for (j = 0; j < num_states; j++) {
            double val = 0;
            for (k = 0; k < num_states; k++)
                val += exptime[i*num_states+k] * inv_eigenvectors[k*num_states+j];
            // make sure that trans_matrix are non-negative
            assert(val >= -0.001);
            val = fabs(val);
            trans_matrix[i*num_states+j] = val;
            row_sum += val;
        }
        assert(fabs(row_sum-1.0) < 1e-4);
    }

    delete [] exptime;
}


/****************************************************/
/*      HELPER FUNCTIONS                            */
/****************************************************/

/* BQM: Ziheng Yang code which fixed old matinv function */
int matinv (double x[], int n, int m, double space[])
{
    /* x[n*m]  ... m>=n
       space[n].  This puts the fabs(|x|) into space[0].  Check and calculate |x|.
       Det may have the wrong sign.  Check and fix.
    */
    int i,j,k;
    int *irow=(int*) space;
    double ee=1e-100, t,t1,xmax, det=1;

    for (i=0; i<n; i++) irow[i]=i;

    for (i=0; i<n; i++)  {
        xmax = fabs(x[i*m+i]);
        for (j=i+1; j<n; j++)
            if (xmax<fabs(x[j*m+i]))
            {
                xmax = fabs(x[j*m+i]);
                irow[i]=j;
            }
        det *= x[irow[i]*m+i];
        if (xmax < ee)   {
            cout << endl << "xmax = " << xmax << " close to zero at " << i+1 << "!\t" << endl;
            exit(-1);
        }
        if (irow[i] != i) {
            for (j=0; j < m; j++) {
                t = x[i*m+j];
                x[i*m+j] = x[irow[i]*m+j];
                x[irow[i]*m+j] = t;
            }
        }
        t = 1./x[i*m+i];
        for (j=0; j < n; j++) {
            if (j == i) continue;
            t1 = t*x[j*m+i];
            for (k=0; k<m; k++)  x[j*m+k] -= t1*x[i*m+k];
            x[j*m+i] = -t1;
        }
        for (j=0; j < m; j++)   x[i*m+j] *= t;
        x[i*m+i] = t;
    }                            /* for(i) */
    for (i=n-1; i>=0; i--) {
        if (irow[i] == i) continue;
        for (j=0; j < n; j++)  {
            t = x[j*m+i];
            x[j*m+i] = x[j*m + irow[i]];
            x[j*m + irow[i]] = t;
        }
    }
    space[0]=det;
    return(0);
}

int computeStateFreqFromQMatrix (double Q[], double pi[], int n, double space[])
{
    /* from rate matrix Q[] to pi, the stationary frequencies:
       Q' * pi = 0     pi * 1 = 1
       space[] is of size n*(n+1).
    */
    int i,j;
    double *T = space;      /* T[n*(n+1)]  */

    for (i=0;i<n+1;i++) T[i]=1;
    for (i=1;i<n;i++) {
        for (j=0;j<n;j++)
            T[i*(n+1)+j] =  Q[j*n+i];     /* transpose */
        T[i*(n+1)+n] = 0.;
    }
    matinv(T, n, n+1, pi);
    for (i=0;i<n;i++)
        pi[i] = T[i*(n+1)+n];
    return (0);
}
/* End of Ziheng Yang code */

int matby (double a[], double b[], double c[], int n,int m,int k)
/* a[n*m], b[m*k], c[n*k]  ......  c = a*b
*/
{
    int i,j,i1;
    double t;
    for (i = 0; i < n; i++)
        for (j = 0; j < k; j++) {
            for (i1=0,t=0; i1<m; i1++) t+=a[i*m+i1]*b[i1*k+j];
            c[i*k+j] = t;
        }
    return (0);
}

int matexp (double Q[], double t, int n, int TimeSquare, double space[])
{
    /* This calculates the matrix exponential P(t) = exp(t*Q).
       Input: Q[] has the rate matrix, and t is the time or branch length.
              TimeSquare is the number of times the matrix is squared and should
              be from 5 to 31.
       Output: Q[] has the transition probability matrix, that is P(Qt).
       space[n*n]: required working space.
          P(t) = (I + Qt/m + (Qt/m)^2/2)^m, with m = 2^TimeSquare.
       T[it=0] is the current matrix, and T[it=1] is the squared result matrix,
       used to avoid copying matrices.
       Use an even TimeSquare to avoid one round of matrix copying.
    */
    int it, i;
    double *T[2];

    if (TimeSquare<2 || TimeSquare>31) cout << "TimeSquare not good" << endl;
    T[0]=Q;
    T[1]=space;
    for (i=0; i<n*n; i++)  T[0][i] = ldexp( Q[i]*t, -TimeSquare );

    matby (T[0], T[0], T[1], n, n, n);
    for (i=0; i<n*n; i++)  T[0][i] += T[1][i]/2;
    for (i=0; i<n; i++)  T[0][i*n+i] ++;

    for (i=0,it=0; i<TimeSquare; i++) {
        it = !it;
        matby (T[1-it], T[1-it], T[it], n, n, n);
    }
    if (it==1)
        for (i=0;i<n*n;i++) Q[i]=T[1][i];
    return(0);
}