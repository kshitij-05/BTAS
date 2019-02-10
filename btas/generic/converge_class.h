#ifndef BTAS_GENERIC_CONV_BASE_CLASS
#define BTAS_GENERIC_CONV_BASE_CLASS
#include <btas/generic/dot_impl.h>
#include <vector>
#include "btas/varray/varray.h"

namespace btas {
  /**
    \brief Default class to deciding when the ALS problem is converged
    Instead of using the change in the loss function
    \f$ \Delta \| \mathcal{T} - \mathcal{\hat{T}} \| \leq \epsilon \f$
    where \f$ \mathcal{\hat{T}} = \sum_{r=1}^R a_1 \circ a_2 \circ \dots \circ a_N \f$
    check the difference in the sum of average elements in factor matrices
    \f$ \sum_n^{ndim} \frac{\|A^{i}_n - A^{i+1}_n\|}{dim(A^{i}_n} \leq \epsilon \f$
  **/
  template <typename Tensor>
  class NormCheck{

  public:
    /// constructor for the base convergence test object
    /// \param[in] tol tolerance for ALS convergence
    explicit NormCheck(double tol = 1e-3): tol_(tol){
    }

    ~NormCheck() = default;

    /// Function to check convergence of the ALS problem
    /// convergence when \f$ \sum_n^{ndim} \frac{\|A^{i}_n - A^{i+1}_n\|}{dim(A^{i}_n} \leq \epsilon \f$
    /// \param[in] btas_factors Current set of factor matrices
    bool operator () (const std::vector<Tensor> & btas_factors){
      auto ndim = btas_factors.size() - 1;
      if (prev.empty() || prev[0].size() != btas_factors[0].size()){
        prev.clear();
        for(int i = 0; i < ndim; ++i){
          prev.push_back(Tensor(btas_factors[i].range()));
          prev[i].fill(0.0);
        }
      }

      auto diff = 0.0;
      rank_ = btas_factors[0].extent(1);
      for(int r = 0; r < ndim; ++r){
        auto elements = btas_factors[r].size();
        auto change = prev[r] - btas_factors[r];
        diff += std::sqrt(btas::dot(change, change)/elements);
        prev[r] = btas_factors[r];
      }

      if(diff < this->tol_){
        return true;
      }
      return false;
    }

  private:
    double tol_;
    std::vector<Tensor> prev;     // Set of previous factor matrices
    int ndim;                     // Number of factor matrices
    int rank_;               // Rank of the CP problem
  };

  // From Tensor Toolbox : 
  // The "fit" is defined as 1 - norm(X-full(M))/norm(X) and is
  // loosely the proportion of the data described by the CP model, i.e., a
  // fit of 1 is perfect.
  template<typename Tensor>
  class FitCheck{
  public:
    /// constructor for the base convergence test object
    /// \param[in] tol tolerance for ALS convergence
    explicit FitCheck(double tol = 1e-4): tol_(tol){
    }

    ~FitCheck() = default;

    /// Function to check convergence of the ALS problem
    /// convergence when \f$ \|T - \hat{T}^{i+1}_n\|}{dim(A^{i}_n} \leq \epsilon \f$
    /// \param[in] btas_factors Current set of factor matrices
    bool operator () (const std::vector<Tensor> & btas_factors){
      auto n = btas_factors.size() - 2;
      auto size = btas_factors[n].size();
      auto rank = btas_factors[n].extent(1);
      Tensor temp(btas_factors[n+1].range());
      temp.fill(0.0);
      auto * ptr_A = btas_factors[n].data();
      auto * ptr_MtKRP = MtKRP_.data();
      for(int i = 0; i < size; ++i){
        *(ptr_MtKRP + i) *= *(ptr_A + i);
      }

      auto * ptr_temp = temp.data();
      for(int i = 0; i < size; ++i){
        *(ptr_temp + i % rank) += *(ptr_MtKRP + i);
      }

      size = temp.size();
      ptr_A = btas_factors[n+1].data();
      double iprod = 0.0;
      for(int i = 0; i < size; ++i){
        iprod += *(ptr_temp + i) * *(ptr_A + i);
      }

      double normFactors = norm(btas_factors);
      double normResidual = sqrt(normT_ * normT_ + normFactors * normFactors - 2 * iprod);
      double fit = 1 - (normResidual / normT_);

      double fitChange = abs(fitOld_ - fit);
      fitOld_ = fit;
      std::cout << fit << "\t" << fitChange << std::endl;
      if(fitChange < tol_) {
        iter_ = 0;
        return true;
      }

      ++iter_;
      return false;
    }

    void set_norm(double normT){
      normT_ = normT;
    }

    void set_MtKRP(Tensor & MtKRP){
      MtKRP_ = MtKRP;
    }

  private:
    double tol_;
    double fitOld_ = -1.0;
    double normT_;
    int iter_ = 0;
    Tensor MtKRP_;

    double norm(const std::vector<Tensor> & btas_factors){
      auto rank = btas_factors[0].extent(1);
      auto n = btas_factors.size() - 1;
      Tensor coeffMat(rank, rank);
      auto temp = btas_factors[n];
      temp.resize(Range{Range1{rank}, Range1{1}});
      gemm(CblasNoTrans, CblasTrans, 1.0, temp, temp, 0.0, coeffMat);

      for(int i = 0; i < n; ++i){
        Tensor temp(rank, rank);
        gemm(CblasTrans, CblasNoTrans, 1.0, btas_factors[i], btas_factors[i], 0.0, temp);
        auto * ptr_coeff = coeffMat.data();
        auto * ptr_temp = temp.data();
        for(int i = 0; i < rank * rank; ++i){
          *(ptr_coeff + i) *= *(ptr_temp + i);
        }
      }

      auto nrm = 0.0;
      for(auto & i: coeffMat){
        nrm += i;
      }
      return sqrt(abs(nrm));
    }
  };

} //namespace btas
#endif