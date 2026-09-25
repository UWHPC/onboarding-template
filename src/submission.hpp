#pragma once

#include <cstddef>
#include <vector>

// Starter Grid for the 2D heat-diffusion problem.
//
// The evaluation harness uses operator() to set initial conditions and to read
// results; it never touches your internal storage. Keep this interface,
// everything else is yours.
class Grid {
private:
  std::size_t rows_;
  std::size_t cols_;
  std::vector<double> grid_vec_;

public:

  Grid(std::size_t rows, std::size_t cols) : rows_{rows} , cols_{cols} , grid_vec_(rows*cols , 0.0){}

  //using .at() to do the index overflow check automatically and throw an exception when the index is out of bounds instead of doing a manual statement , the .at() makes every cell into 1 read , 1 comparison and 1 throw which is a lot of operations instead of the standard [] which just does 1 read or 1 write . The out of bound can be compared in the actual code itself instead of doing it in the inside loop 
  double& operator()(std::size_t i, std::size_t j){
    return grid_vec_[i*cols_ + j];
  }
  double  operator()(std::size_t i, std::size_t j) const{
    return grid_vec_[i*cols_ + j];
  }

  //making functions to return the width and the height to make the for loop doable or else , I would not be able to have the size for the grids 
  std::size_t width() const{
    return cols_ ; 
  }
  
  std::size_t height() const{
    return rows_ ; 
  }
};  

// Apply the five-point stencil over all interior points, copying the boundary
// values unchanged from old_grid to new_grid. Implement your solution here.
void apply_stencil(const Grid& old_grid, Grid& new_grid){
  //the first version I wrote , I basically did the if statement comparison inside of the for loops which is not the right way as I realized because, we only need it for the boundary cells which cause the machine to execute the conditional on the inner grid too which is the largest chunk , causing this iteration of code to be slow . 
  if(old_grid.height() == 1 ^ old_grid.width() == 1){
    new_grid(0,0) = old_grid(0,0); 
  }

  //have to make the boundaries of the grids just copy the values that were before it . 
  for (std::size_t i = 0; i < old_grid.height(); i++)
  {
    new_grid(i,0) = old_grid(i,0);
    new_grid(i,old_grid.width()-1) = old_grid(i,old_grid.width()-1);
  }

  for (std::size_t j = 0; j < old_grid.width(); j++)
  {
    new_grid(0,j) = old_grid(0,j);
    new_grid(old_grid.height()-1,j) = old_grid(old_grid.height()-1,j); 
  }

  //have to make every other cell inside the grid use the formula of dissipation 
  for (std::size_t i = 1; i < old_grid.height()-1 ; i++)
  { 
    
    #pragma omp simd
    for (std::size_t j = 1; j < old_grid.width()-1 ; j++)
    {
      new_grid(i,j) = 0.5 * old_grid(i,j) + 
                      0.125 * ( old_grid(i+1,j) + old_grid(i-1 ,j) + old_grid(i ,j+1) + old_grid(i,j-1) ) ;
    }
    
  }  
  
}
