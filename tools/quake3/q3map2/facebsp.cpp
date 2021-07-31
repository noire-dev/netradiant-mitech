/* -------------------------------------------------------------------------------

   Copyright (C) 1999-2007 id Software, Inc. and contributors.
   For a list of contributors, see the accompanying CONTRIBUTORS file.

   This file is part of GtkRadiant.

   GtkRadiant is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   GtkRadiant is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GtkRadiant; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

   ----------------------------------------------------------------------------------

   This code has been altered significantly from its original form, to support
   several games based on the Quake III Arena engine, in the form of "Q3Map2."

   ------------------------------------------------------------------------------- */



/* dependencies */
#include "q3map2.h"



int c_faceLeafs;


/*
   ================
   AllocBspFace
   ================
 */
face_t  *AllocBspFace( void ) {
	return safe_calloc( sizeof( face_t ) );
}



/*
   ================
   FreeBspFace
   ================
 */
void    FreeBspFace( face_t *f ) {
	if ( f->w ) {
		FreeWinding( f->w );
	}
	free( f );
}



/*
   SelectSplitPlaneNum()
   finds the best split plane for this node
 */

static void SelectSplitPlaneNum( node_t *node, face_t *list, int *splitPlaneNum, int *compileFlags ){
	face_t      *split;
	face_t      *check;
	face_t      *bestSplit;
	int splits, facing, front, back;
	int value, bestValue;
	int i;
	float dist;
	int planenum;
	float sizeBias;

	//int frontC,backC,splitsC,facingC;


	/* ydnar: set some defaults */
	*splitPlaneNum = -1; /* leaf */
	*compileFlags = 0;

	/* ydnar 2002-06-24: changed this to split on z-axis as well */
	/* ydnar 2002-09-21: changed blocksize to be a vector, so mappers can specify a 3 element value */

	/* if it is crossing a block boundary, force a split */
	for ( i = 0; i < 3; i++ )
	{
		if ( blockSize[ i ] <= 0 ) {
			continue;
		}
		dist = blockSize[ i ] * ( floor( node->minmax.mins[ i ] / blockSize[ i ] ) + 1 );
		if ( node->minmax.maxs[ i ] > dist ) {
			planenum = FindFloatPlane( g_vector3_axes[i], dist, 0, NULL );
			*splitPlaneNum = planenum;
			return;
		}
	}

	/* pick one of the face planes */
	bestValue = -99999;
	bestSplit = list;


	// div0: this check causes detail/structural mixes
	//for( split = list; split; split = split->next )
	//	split->checked = false;

	for ( split = list; split; split = split->next )
	{
		//if ( split->checked )
		//	continue;

		const plane_t& plane = mapplanes[ split->planenum ];
		splits = 0;
		facing = 0;
		front = 0;
		back = 0;
		for ( check = list ; check ; check = check->next ) {
			if ( check->planenum == split->planenum ) {
				facing++;
				//check->checked = true;	// won't need to test this plane again
				continue;
			}
			const EPlaneSide side = WindingOnPlaneSide( *check->w, plane.plane );
			if ( side == eSideCross ) {
				splits++;
			}
			else if ( side == eSideFront ) {
				front++;
			}
			else if ( side == eSideBack ) {
				back++;
			}
		}

		if ( bspAlternateSplitWeights ) {
			// from 27

			//Bigger is better
			sizeBias = WindingArea( *split->w );

			//Base score = 20000 perfectly balanced
			value = 20000 - ( abs( front - back ) );
			value -= plane.counter; // If we've already used this plane sometime in the past try not to use it again
			value -= facing ;       // if we're going to have alot of other surfs use this plane, we want to get it in quickly.
			value -= splits * 5;        //more splits = bad
			value +=  sizeBias * 10; //We want a huge score bias based on plane size
		}
		else
		{
			value =  5 * facing - 5 * splits; // - abs(front-back);
			if ( plane.type < ePlaneNonAxial ) {
				value += 5;       // axial is better
			}
		}

		value += split->priority;       // prioritize hints higher

		if ( value > bestValue ) {
			bestValue = value;
			bestSplit = split;
			//frontC=front;
			//backC=back;
			//splitsC=splits;
			//facingC=facing;
		}
	}

	/* nothing, we have a leaf */
	if ( bestValue == -99999 ) {
		return;
	}

	//Sys_FPrintf( SYS_VRB, "F: %d B:%d S:%d FA:%ds\n", frontC, backC, splitsC, facingC );

	/* set best split data */
	*splitPlaneNum = bestSplit->planenum;
	*compileFlags = bestSplit->compileFlags;

#if 0
	if ( bestSplit->compileFlags & C_DETAIL ) {
		for ( split = list; split; split = split->next )
			if ( !( split->compileFlags & C_DETAIL ) ) {
				Sys_FPrintf( SYS_ERR, "DON'T DO SUCH SPLITS (1)\n" );
			}
	}
	if ( ( node->compileFlags & C_DETAIL ) && !( bestSplit->compileFlags & C_DETAIL ) ) {
		Sys_FPrintf( SYS_ERR, "DON'T DO SUCH SPLITS (2)\n" );
	}
#endif

	if ( *splitPlaneNum > -1 ) {
		mapplanes[ *splitPlaneNum ].counter++;
	}
}



/*
   CountFaceList()
   counts bsp faces in the linked list
 */

int CountFaceList( face_t *list ){
	int c;


	c = 0;
	for ( ; list != NULL; list = list->next )
		c++;
	return c;
}



/*
   BuildFaceTree_r()
   recursively builds the bsp, splitting on face planes
 */

void BuildFaceTree_r( node_t *node, face_t *list ){
	face_t      *split;
	face_t      *next;
	face_t      *newFace;
	face_t      *childLists[2];
	winding_t   *frontWinding, *backWinding;
	int i;
	int splitPlaneNum, compileFlags;
#if 0
	bool isstruct = false;
#endif


	/* count faces left */
	i = CountFaceList( list );

	/* select the best split plane */
	SelectSplitPlaneNum( node, list, &splitPlaneNum, &compileFlags );

	/* if we don't have any more faces, this is a node */
	if ( splitPlaneNum == -1 ) {
		node->planenum = PLANENUM_LEAF;
		node->has_structural_children = false;
		c_faceLeafs++;
		return;
	}

	/* partition the list */
	node->planenum = splitPlaneNum;
	node->compileFlags = compileFlags;
	node->has_structural_children = !( compileFlags & C_DETAIL ) && !node->opaque;
	const plane_t& plane = mapplanes[ splitPlaneNum ];
	childLists[0] = NULL;
	childLists[1] = NULL;

	for ( split = list; split; split = next )
	{
		/* set next */
		next = split->next;

		/* don't split by identical plane */
		if ( split->planenum == node->planenum ) {
			FreeBspFace( split );
			continue;
		}

#if 0
		if ( !( split->compileFlags & C_DETAIL ) ) {
			isstruct = true;
		}
#endif

		/* determine which side the face falls on */
		const EPlaneSide side = WindingOnPlaneSide( *split->w, plane.plane );

		/* switch on side */
		if ( side == eSideCross ) {
			ClipWindingEpsilonStrict( *split->w, plane.plane, CLIP_EPSILON * 2,
			                          frontWinding, backWinding ); /* strict; if no winding is left, we have a "virtually identical" plane and don't want to split by it */
			if ( frontWinding ) {
				newFace = AllocBspFace();
				newFace->w = frontWinding;
				newFace->next = childLists[0];
				newFace->planenum = split->planenum;
				newFace->priority = split->priority;
				newFace->compileFlags = split->compileFlags;
				childLists[0] = newFace;
			}
			if ( backWinding ) {
				newFace = AllocBspFace();
				newFace->w = backWinding;
				newFace->next = childLists[1];
				newFace->planenum = split->planenum;
				newFace->priority = split->priority;
				newFace->compileFlags = split->compileFlags;
				childLists[1] = newFace;
			}
			FreeBspFace( split );
		}
		else if ( side == eSideFront ) {
			split->next = childLists[0];
			childLists[0] = split;
		}
		else if ( side == eSideBack ) {
			split->next = childLists[1];
			childLists[1] = split;
		}
	}


	// recursively process children
	for ( i = 0 ; i < 2 ; i++ ) {
		node->children[i] = AllocNode();
		node->children[i]->parent = node;
		node->children[i]->minmax = node->minmax;
	}

	for ( i = 0 ; i < 3 ; i++ ) {
		if ( plane.normal()[i] == 1 ) {
			node->children[0]->minmax.mins[i] = plane.dist();
			node->children[1]->minmax.maxs[i] = plane.dist();
			break;
		}
		if ( plane.normal()[i] == -1 ) {
			node->children[0]->minmax.maxs[i] = -plane.dist();
			node->children[1]->minmax.mins[i] = -plane.dist();
			break;
		}
	}

#if 0
	if ( ( node->compileFlags & C_DETAIL ) && isstruct ) {
		Sys_FPrintf( SYS_ERR, "I am detail, my child is structural, this is a wtf1\n", node->has_structural_children );
	}
#endif

	for ( i = 0 ; i < 2 ; i++ ) {
		BuildFaceTree_r( node->children[i], childLists[i] );
		node->has_structural_children |= node->children[i]->has_structural_children;
	}

#if 0
	if ( ( node->compileFlags & C_DETAIL ) && !( node->children[0]->compileFlags & C_DETAIL ) && node->children[0]->planenum != PLANENUM_LEAF ) {
		Sys_FPrintf( SYS_ERR, "I am detail, my child is structural\n", node->has_structural_children );
	}
	if ( ( node->compileFlags & C_DETAIL ) && isstruct ) {
		Sys_FPrintf( SYS_ERR, "I am detail, my child is structural, this is a wtf2\n", node->has_structural_children );
	}
#endif
}


/*
   ================
   FaceBSP

   List will be freed before returning
   ================
 */
tree_t *FaceBSP( face_t *list ) {
	Sys_FPrintf( SYS_VRB, "--- FaceBSP ---\n" );

	tree_t *tree = AllocTree();

	int count = 0;
	for ( const face_t *face = list; face != NULL; face = face->next )
	{
		WindingExtendBounds( *face->w, tree->minmax );
		count++;
	}
	Sys_FPrintf( SYS_VRB, "%9d faces\n", count );

	for ( plane_t& plane : mapplanes )
	{
		plane.counter = 0;
	}

	tree->headnode = AllocNode();
	tree->headnode->minmax = tree->minmax;
	c_faceLeafs = 0;

	BuildFaceTree_r( tree->headnode, list );

	Sys_FPrintf( SYS_VRB, "%9d leafs\n", c_faceLeafs );

	return tree;
}



/*
   MakeStructuralBSPFaceList()
   get structural brush faces
 */

face_t *MakeStructuralBSPFaceList( brush_t *list ){
	brush_t     *b;
	int i;
	side_t      *s;
	winding_t   *w;
	face_t      *f, *flist;


	flist = NULL;
	for ( b = list; b != NULL; b = b->next )
	{
		if ( !deepBSP && b->detail ) {
			continue;
		}

		for ( i = 0; i < b->numsides; i++ )
		{
			/* get side and winding */
			s = &b->sides[ i ];
			w = s->winding;
			if ( w == NULL ) {
				continue;
			}

			/* ydnar: skip certain faces */
			if ( s->compileFlags & C_SKIP ) {
				continue;
			}

			/* allocate a face */
			f = AllocBspFace();
			f->w = CopyWinding( w );
			f->planenum = s->planenum & ~1;
			f->compileFlags = s->compileFlags;  /* ydnar */
			if ( b->detail ) {
				f->compileFlags |= C_DETAIL;
			}

			/* ydnar: set priority */
			f->priority = 0;
			if ( f->compileFlags & C_HINT ) {
				f->priority += HINT_PRIORITY;
			}
			if ( f->compileFlags & C_ANTIPORTAL ) {
				f->priority += ANTIPORTAL_PRIORITY;
			}
			if ( f->compileFlags & C_AREAPORTAL ) {
				f->priority += AREAPORTAL_PRIORITY;
			}
			if ( f->compileFlags & C_DETAIL ) {
				f->priority += DETAIL_PRIORITY;
			}

			/* get next face */
			f->next = flist;
			flist = f;
		}
	}

	return flist;
}



/*
   MakeVisibleBSPFaceList()
   get visible brush faces
 */

face_t *MakeVisibleBSPFaceList( brush_t *list ){
	brush_t     *b;
	int i;
	side_t      *s;
	winding_t   *w;
	face_t      *f, *flist;


	flist = NULL;
	for ( b = list; b != NULL; b = b->next )
	{
		if ( !deepBSP && b->detail ) {
			continue;
		}

		for ( i = 0; i < b->numsides; i++ )
		{
			/* get side and winding */
			s = &b->sides[ i ];
			w = s->visibleHull;
			if ( w == NULL ) {
				continue;
			}

			/* ydnar: skip certain faces */
			if ( s->compileFlags & C_SKIP ) {
				continue;
			}

			/* allocate a face */
			f = AllocBspFace();
			f->w = CopyWinding( w );
			f->planenum = s->planenum & ~1;
			f->compileFlags = s->compileFlags;  /* ydnar */
			if ( b->detail ) {
				f->compileFlags |= C_DETAIL;
			}

			/* ydnar: set priority */
			f->priority = 0;
			if ( f->compileFlags & C_HINT ) {
				f->priority += HINT_PRIORITY;
			}
			if ( f->compileFlags & C_ANTIPORTAL ) {
				f->priority += ANTIPORTAL_PRIORITY;
			}
			if ( f->compileFlags & C_AREAPORTAL ) {
				f->priority += AREAPORTAL_PRIORITY;
			}
			if ( f->compileFlags & C_DETAIL ) {
				f->priority += DETAIL_PRIORITY;
			}

			/* get next face */
			f->next = flist;
			flist = f;
		}
	}

	return flist;
}
