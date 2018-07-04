
<!-- README.md is generated from README.Rmd. Please edit that file -->
rmaverick
=========

<!--
[![Build Status](https://travis-ci.org/bobverity/rmaverick.png?branch=develop)](https://travis-ci.org/bobverity/rmaverick)
[![Coverage status](https://codecov.io/gh/bobverity/rmaverick/branch/develop/graph/badge.svg)](https://codecov.io/github/bobverity/rmaverick?branch=develop)
[![AppVeyor Build Status](https://ci.appveyor.com/api/projects/status/github/bobverity/rmaverick?branch=develop&svg=true)](https://ci.appveyor.com/project/bobverity/rmaverick)
-->
The goal of rmaverick is to infer population structure from genetic data. What makes rmaverick different from other similar programs is its ability to estimate the *evidence* for different numbers of sub-populations (K), and even different evolutionary models, through a method called generalised thermodynamic integration (GTI). It also implements some advanced MCMC methods that ensure reliable results.

Installation
------------

rmaverick relies on the [Rcpp](https://cran.r-project.org/web/packages/Rcpp/index.html) package, which requires the following OS-specific steps:

-   Windows
    -   Download and install the appropriate version of [Rtools](https://cran.rstudio.com/bin/windows/Rtools/) for your version of R. On installation, ensure you check the box to arrange your system PATH as recommended by Rtools
-   Mac OS X
    -   Download and install [XCode](http://itunes.apple.com/us/app/xcode/id497799835?mt=12)
    -   Within XCode go to Preferences : Downloads and install the Command Line Tools
-   Linux (Debian/Ubuntu)
    -   Install the core software development utilities required for R package development as well as LaTeX by executing

            sudo apt-get install r-base-dev texlive-full

Next, in R, ensure that you have the devtools package installed by running

``` r
install.packages("devtools", repos='http://cran.us.r-project.org')
```

Then we can simply install the rmaverick package directly from GitHub by running

``` r
devtools::install_github("bobverity/rmaverick")
```

Finally, we need to load the package. This makes the rmaverick functions available to R, and therefore needs to be done each time R or RStudio is opened, usually at the top of a script.

``` r
library(rmaverick)
```

Example analysis
----------------

This example demonstrates the process of importing data, running the main MCMC, diagnosing good and bad MCMC behaviour, comparing different models, and producing basic outputs.

### Simulate some data

rmverick contains built-in functions for simulating from the exact same models used in the inference step. We will simulate a data set of 100 individuals, each genotyped at 20 loci and originating from 3 distinct subpopulations. We will simulate from the no-admixture model, meaning all loci in an individual are assumed to have originated from the same subpopulation.

``` r
mysim <- sim_data(n = 100, loci = 20, K = 3, admix_on = FALSE)
```

Running `names(mysim)` we see that the simulated data contains several objects:

``` r
names(mysim)
#> [1] "data"         "allele_freqs" "admix_freqs"  "group"
```

As well as the raw data we have a record of the allele frequencies, admixture frequencies, and grouping that were used in generating these data.

Running `head(mysim$data)` we can see the general data format used by rmaverick:

``` r
head(mysim$data)
#>     ID pop ploidy locus1 locus2 locus3 locus4 locus5 locus6 locus7 locus8
#> 1 ind1   1      2      2      1      5      5      3      3      4      3
#> 2 ind1   1      2      5      2      5      2      2      1      5      3
#> 3 ind2   1      2      2      3      4      5      3      3      4      4
#> 4 ind2   1      2      4      2      1      2      4      1      2      3
#> 5 ind3   1      2      2      1      1      1      2      4      4      5
#> 6 ind3   1      2      3      3      5      2      1      4      2      4
#>   locus9 locus10 locus11 locus12 locus13 locus14 locus15 locus16 locus17
#> 1      5       1       3       5       2       1       5       3       2
#> 2      5       1       5       1       3       1       4       3       2
#> 3      5       1       3       1       3       3       4       3       2
#> 4      3       1       3       5       1       2       4       3       1
#> 5      3       1       5       4       3       5       4       2       4
#> 6      2       1       2       1       5       5       4       3       3
#>   locus18 locus19 locus20
#> 1       4       1       2
#> 2       4       1       2
#> 3       1       1       2
#> 4       1       1       2
#> 5       1       3       2
#> 6       4       1       2
```

Samples are in rows and loci are in columns, meaning for polyploid individuals multiple rows can be used for the same individual. There are also several meta-data columns, including the sample ID, the population of origin of the sample and the ploidy. These meta-data columns are optional and can be turned on or off when loading the data into a project.

### Create a project and read in data

rmaverick works with projects, which are essentially nothing more than simple lists containing all the inputs and outputs of a given analysis in one convenient place. We start by creating a project and loading in our data:

``` r
myproj <- mavproject()
myproj <- bind_data(myproj, mysim$data, ID_col = 1, pop_col = 2, ploidy_col = 3)
```

Notice the general format of the `bind_data()` function, which takes the same project as both input and output. This is the general format that most rmaverick functions will take, as it allows the function to modify the project before overwriting the original version. Notice also that we have specified which columns are meta-data, and all other columns are assumed to contain genetic data. We can produce a summary of the project to check that the data has been loaded in correctly:

``` r
summary(myproj)
#>                Length Class      Mode   
#> data           23     data.frame list   
#> data_processed  6     -none-     list   
#> parameter_sets  0     -none-     NULL   
#> active_set      1     -none-     numeric
#> output          2     -none-     list
```

### Define parameters and run basic MCMC

We can define different evolutionary models using different parameter sets. Our first set will represent a simple no-admixture model (i.e. the correct model).

``` r
myproj <- new_set(myproj, name = "no-admixture", admix_on = FALSE)
```

Producing a summary of the project we can see which is the current active set, and the key parameters of this set.

``` r
summary(myproj)
#>                Length Class      Mode   
#> data           23     data.frame list   
#> data_processed  6     -none-     list   
#> parameter_sets  1     -none-     list   
#> active_set      1     -none-     numeric
#> output          2     -none-     list
```

Now we are ready to run a basic MCMC. We will start by exploring values of K from 1 to 3, using 1000 burn-in iterations and 1000 sampling iterations. By default the MCMC has `auto_converge` turned on, meaning it will test for convergence every `burnin/10` iterations and will exit if convergence is reached. Hence it is generally a good idea to set `burnin` on the high end, as the MCMC will adjust this number down if needed. The number of sampling iterations can also be tuned if needed, and our aim should be to obtain enough samples that our posterior estimates are accurate to a given tolerance level, but not to waste time running the MCMC for long periods past this point. We will look into this parameter again once the initial MCMC has completed. The most unfamiliar parameter for most users will be the number of "rungs". rmaverick runs multiple MCMC chains simultaneously, each at a different rung on a temperature ladder. The hot chains serve two purposes: 1) they improve MCMC mixing by allowing it to explore the space more freely, 2) they are central to the GTI method of estimating the "evidence" for different models (i.e. comparing different values of K). Finally, for the sake of this document we will run with `pb_markdown=TRUE` to avoid printing large amounts of output, but ordinarily this option should be left off.

``` r
myproj <- run_mcmc(myproj, K = 1:5, burnin = 1e3, samples = 1e3, rungs = 10, pb_markdown =  TRUE)
#> Calculating exact solution for K = 1
#>    completed in 0.0019157 seconds
#> 
#> Running MCMC for K = 2
#> Burn-in phase
#> 
  |                                                                       
  |=================================================================| 100%
#>    converged within 100 iterations
#> Sampling phase
#> 
  |                                                                       
  |=================================================================| 100%
#>    completed in 0.72835 seconds
#> 
#> Running MCMC for K = 3
#> Burn-in phase
#> 
  |                                                                       
  |=================================================================| 100%
#>    converged within 100 iterations
#> Sampling phase
#> 
  |                                                                       
  |=================================================================| 100%
#>    completed in 0.996216 seconds
#> 
#> Running MCMC for K = 4
#> Burn-in phase
#> 
  |                                                                       
  |=================================================================| 100%
#>    converged within 100 iterations
#> Sampling phase
#> 
  |                                                                       
  |=================================================================| 100%
#>    completed in 1.25589 seconds
#> 
#> Running MCMC for K = 5
#> Burn-in phase
#> 
  |                                                                       
  |=================================================================| 100%
#>    converged within 100 iterations
#> Sampling phase
#> 
  |                                                                       
  |=================================================================| 100%
#>    completed in 1.51453 seconds
#> 
#> Processing results
#> Total run-time: 5.16 seconds
```

You will have seen that the solution for K=1 is almost instantaneous, as no MCMC is required in this case. In the example above we also saw that the MCMC converged within far fewer than the full 1000 burn-in iterations.

Next, we need to check that we are happy with the behaviour of our MCMC. We will do this in three ways:

1.  By looking at levels of auto-correlation
2.  By measuring the accuracy of posterior estimates
3.  By looking at the GTI "path"

#### 1. Measuring auto-correlation

TODO!

#### 2. Posterior accuracy

TODO!

#### 3. GTI path

The GTI method estimates the "evidence" for a given model by combining information across multiple temperature rungs. These rungs provide a series of point estimates that together make a "path". We can visualise this path using the `plot_GTI_path()` function:

``` r
plot_GTI_path(myproj, K = 3)
```

![](README-unnamed-chunk-14-1.png)

The final evidence estimate is calculated from the area under the path, and therefore it is important that this path is relatively smooth. We can modify the smoothness of the path in two ways: 1) by increasing the number of `rungs` used in the MCMC, 2) by changing the value of `GTI_pow` which controls the curvature of the path. In the example above we have a good number of rungs, although we might consider using a lower `GTI_pow` to reduce the curvature of the path slightly. But overall this path is pretty good, so there is no need to re-run the MCMC.

Next, we look at our evidence estimates in log space:

``` r
plot_logevidence_K(myproj)
```

![](README-unnamed-chunk-15-1.png)

We can see a clear signal for K=3 or above, and the 95% credible intervals are nice and tight. We can also plot the full posterior distribution of K:

``` r
plot_posterior_K(myproj)
```

![](README-unnamed-chunk-16-1.png)

This plot is usually more straightfoward to interpret, as it is in linear space and be understood in terms of ordinary probability. In this example we can see strong evidence for K=3, with more than 99% of the posterior mass attributed to this value. If we had seen wide credible intervals at this stage then it would have be worth repeating the MCMC with a larger number of samples, but in our case the result is clear and so there is no need.

### Plot results

Text

``` r
plot_qmatrix_ind(myproj, K = 2)
```

![](README-unnamed-chunk-17-1.png)

``` r
plot_qmatrix_ind(myproj, K = 3)
```

![](README-unnamed-chunk-17-2.png)

### Admixture model

Text

``` r
#myproj <- new_set(myproj, name = "admixture", admix_on = TRUE, estimate_alpha = TRUE)

#myproj <- run_mcmc(myproj, K = 1:5, burnin = 1e3, samples = 1e3, rungs = 10, silent = TRUE)

#plot_loglike_quantiles(myproj, K = 3)

#plot_GTI_path(myproj, K = 3)
```

``` r
#plot_logevidence_K(myproj)
#plot_posterior_K(myproj)
```

``` r
for (i in 2:4) {
  #plot_qmatrix_ind(myproj, K = i)
}
```

### Final notes

-   If we are not interested in estimating K or comparing models then there is technically no need to use multiple temperature rungs, and so the MCMC can be made to run faster by setting `rungs=1`.

-   Can be run on a cluster (notes to follow soon)
